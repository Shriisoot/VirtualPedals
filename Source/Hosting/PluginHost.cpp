#include "PluginHost.h"
#include "../Presets/PresetManager.h"

namespace vp
{

//==============================================================================
class VstPedal::EditorWindow : public juce::DocumentWindow
{
public:
    EditorWindow (const juce::String& name, juce::Component* editor, std::function<void()> onClose)
        : juce::DocumentWindow (name, juce::Colours::darkgrey, juce::DocumentWindow::closeButton),
          closeFn (std::move (onClose))
    {
        setUsingNativeTitleBar (true);
        setContentOwned (editor, true);
        setResizable (true, false);
        centreWithSize (getWidth(), getHeight());
        setVisible (true);
    }

    void closeButtonPressed() override
    {
        if (closeFn) closeFn();
    }

private:
    std::function<void()> closeFn;
};

void VstPedal::openEditor()
{
    if (instance == nullptr || ! instance->hasEditor())
        return;
    if (editorWindow != nullptr)
    {
        editorWindow->toFront (true);
        return;
    }
    if (auto* ed = instance->createEditorIfNeeded())
        editorWindow = std::make_unique<EditorWindow> (desc.name, ed, [this] { closeEditor(); });
}

void VstPedal::closeEditor()
{
    editorWindow = nullptr;
}

//==============================================================================
static juce::File hostDataDir()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("VirtualPedals");
}

PluginHost::PluginHost()
{
    formatManager.addFormat (new juce::VST3PluginFormat());

    hostDataDir().createDirectory();
    if (auto xml = juce::parseXML (listFile().loadFileAsString()))
        knownList.recreateFromXml (*xml);
    if (favFile().existsAsFile())
        favourites.addTokens (favFile().loadFileAsString(), "\n", "");
    const auto pathsFile = hostDataDir().getChildFile ("vstpaths.txt");
    if (pathsFile.existsAsFile())
        extraPaths.addTokens (pathsFile.loadFileAsString(), "\n", "");
}

PluginHost::~PluginHost()
{
    stopTimer();
}

juce::File PluginHost::listFile() const     { return hostDataDir().getChildFile ("knownplugins.xml"); }
juce::File PluginHost::deadMansFile() const { return hostDataDir().getChildFile ("scan-crashed.txt"); }
juce::File PluginHost::favFile() const      { return hostDataDir().getChildFile ("pluginfavs.txt"); }

juce::FileSearchPath PluginHost::getSearchPath() const
{
    juce::FileSearchPath path;
    path.add (juce::File ("C:\\Program Files\\Common Files\\VST3"));
   #if JUCE_WINDOWS
    path.add (juce::File::getSpecialLocation (juce::File::windowsLocalAppData)
                  .getChildFile ("Programs").getChildFile ("Common").getChildFile ("VST3"));
   #endif
    for (const auto& p : extraPaths)
        if (juce::File (p).isDirectory())
            path.add (juce::File (p));
    return path;
}

int PluginHost::scanSingleFile (const juce::File& file)
{
    auto* vst3 = formatManager.getFormat (0);
    juce::OwnedArray<juce::PluginDescription> found;
    knownList.scanAndAddFile (file.getFullPathName(), true, found, *vst3);
    if (auto xml = knownList.createXml())
        listFile().replaceWithText (xml->toString());
    return found.size();
}

void PluginHost::addSearchPath (const juce::File& dir)
{
    extraPaths.addIfNotAlreadyThere (dir.getFullPathName());
    hostDataDir().getChildFile ("vstpaths.txt").replaceWithText (extraPaths.joinIntoString ("\n"));
}

//==============================================================================
void PluginHost::startScan (std::function<void (float, juce::String)> progress, std::function<void()> done)
{
    if (scanning.load())
        return;
    progressCb = std::move (progress);
    doneCb = std::move (done);

    auto* vst3 = formatManager.getFormat (0);
    scanner = std::make_unique<juce::PluginDirectoryScanner> (
        knownList, *vst3, getSearchPath(), true, deadMansFile(), true);
    scanning.store (true);
    startTimer (20); // scan incrementally on the message thread, one plugin per tick
}

void PluginHost::timerCallback()
{
    if (scanner == nullptr)
    {
        stopTimer();
        return;
    }

    juce::String pluginName;
    bool more = true;
    for (int i = 0; i < 2 && more; ++i)
        more = scanner->scanNextFile (true, pluginName);

    if (progressCb)
        progressCb (scanner->getProgress(), pluginName);

    if (! more)
    {
        stopTimer();
        scanner = nullptr;
        scanning.store (false);
        if (auto xml = knownList.createXml())
            listFile().replaceWithText (xml->toString());
        if (doneCb)
            doneCb();
    }
}

juce::Array<juce::PluginDescription> PluginHost::getPlugins (const juce::String& filter) const
{
    juce::Array<juce::PluginDescription> out;
    for (const auto& d : knownList.getTypes())
        if (filter.isEmpty() || d.name.containsIgnoreCase (filter) || d.manufacturerName.containsIgnoreCase (filter))
            out.add (d);

    std::sort (out.begin(), out.end(), [this] (const juce::PluginDescription& a, const juce::PluginDescription& b)
    {
        const bool fa = isFavourite (a.createIdentifierString());
        const bool fb = isFavourite (b.createIdentifierString());
        if (fa != fb) return fa;
        return a.name.compareIgnoreCase (b.name) < 0;
    });
    return out;
}

void PluginHost::setFavourite (const juce::String& pluginId, bool fav)
{
    if (fav) favourites.addIfNotAlreadyThere (pluginId);
    else     favourites.removeString (pluginId);
    favFile().replaceWithText (favourites.joinIntoString ("\n"));
}

bool PluginHost::isFavourite (const juce::String& pluginId) const
{
    return favourites.contains (pluginId);
}

//==============================================================================
std::unique_ptr<VstPedal> PluginHost::createPlugin (const juce::PluginDescription& desc, double sr, int blockSize)
{
    juce::String error;
    auto instance = formatManager.createPluginInstance (desc, sr, blockSize, error);
    if (instance == nullptr)
    {
        juce::Logger::writeToLog ("Plugin load failed: " + error);
        return nullptr;
    }
    return std::make_unique<VstPedal> (std::move (instance), desc);
}

std::unique_ptr<Pedal> PluginHost::restoreFromVar (const juce::var& v, double sr, int blockSize)
{
    const auto descXml = v.getProperty ("pluginDesc", "").toString();
    if (descXml.isEmpty())
        return nullptr;
    juce::PluginDescription desc;
    if (auto xml = juce::parseXML (descXml))
        if (! desc.loadFromXml (*xml))
            return nullptr;
    auto pedal = createPlugin (desc, sr, blockSize);
    if (pedal != nullptr)
        pedal->fromVar (v);
    return pedal;
}

} // namespace vp
