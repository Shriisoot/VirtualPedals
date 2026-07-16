#pragma once
#include "PedalFactory.h"
#include "DspUtil.h"
#include "../Effects/FilterEffects.h"

namespace vp
{

//==============================================================================
// Modular DSP graph powering the Custom Pedal Builder.
//
// Ports: port 0 = stereo audio, port 1 = control/mod input (mono, -1..1).
// Control sources (LFO / Env / Osc used as control) write to their audio out;
// connecting an out-port to a node's port 1 modulates that node's mod target.
// Cycles are legal: nodes in a feedback loop read the upstream node's previous
// block (implicit one-block feedback delay), so feedback paths just work.
struct NodeGraph
{
    //==========================================================================
    struct NodeDef
    {
        const char* type;
        const char* name;
        int numAudioIns;        // 1 = audio in, 2 = two audio ins (mixer)
        bool hasModIn;
        const char* pedalType;  // wraps a factory pedal when non-null
        const char* modParam;   // pedal param driven by the mod input
        std::vector<ParamDef> params; // for primitive (non-pedal) nodes
    };

    static const std::vector<NodeDef>& nodeDefs()
    {
        static const std::vector<NodeDef> defs = {
            { "input",   "Input",     0, false, nullptr, nullptr, {} },
            { "output",  "Output",    1, false, nullptr, nullptr, {} },
            { "gain",    "Gain",      1, true,  nullptr, nullptr, { { "gain", "Gain", "dB", -40.0, 24.0, 0.0 }, { "modamt", "Mod Amt", "", 0.0, 1.0, 0.5 } } },
            { "mixer",   "Mixer",     2, false, nullptr, nullptr, { { "blend", "Blend", "%", 0.0, 100.0, 50.0 } } },
            { "splitter","Splitter",  1, false, nullptr, nullptr, {} },
            { "filter",  "Filter",    1, true,  nullptr, nullptr, { { "freq", "Freq", "Hz", 40.0, 12000.0, 800.0, 0.4 }, { "reso", "Reso", "", 0.5, 8.0, 1.0 },
                                                                    { "ftype", "Type", "", 0.0, 2.0, 0.0, 1.0, { "LP", "BP", "HP" } }, { "modamt", "Mod Amt", "", -1.0, 1.0, 0.5 } } },
            { "eqband",  "EQ Band",   1, false, nullptr, nullptr, { { "freq", "Freq", "Hz", 40.0, 12000.0, 800.0, 0.4 }, { "gaindb", "Gain", "dB", -15.0, 15.0, 0.0 }, { "q", "Q", "", 0.3, 6.0, 1.0 } } },
            { "lfo",     "LFO",       0, false, nullptr, nullptr, { { "rate", "Rate", "Hz", 0.05, 20.0, 1.0, 0.5 }, { "shape", "Shape", "", 0.0, 3.0, 0.0, 1.0, { "Sine", "Tri", "Saw", "Sqr" } } } },
            { "env",     "Env Follow",1, false, nullptr, nullptr, { { "attack", "Attack", "ms", 0.5, 200.0, 8.0, 0.5 }, { "release", "Release", "ms", 10.0, 1000.0, 150.0, 0.5 } } },
            { "osc",     "Oscillator",0, false, nullptr, nullptr, { { "freq", "Freq", "Hz", 20.0, 2000.0, 110.0, 0.4 }, { "shape", "Shape", "", 0.0, 3.0, 0.0, 1.0, { "Sine", "Tri", "Saw", "Sqr" } }, { "level", "Level", "", 0.0, 1.0, 0.3 } } },
            { "stereo",  "Stereo Width", 1, false, nullptr, nullptr, { { "width", "Width", "%", 0.0, 200.0, 100.0 } } },
            { "feedback","Feedback Tap", 1, false, nullptr, nullptr, { { "amount", "Amount", "%", 0.0, 90.0, 30.0 } } },
            // pedal-wrapping nodes: full DSP blocks inside the graph
            { "drive",   "Drive",     1, true,  "overdrive",  "drive", {} },
            { "dist",    "Distortion",1, false, "distortion", nullptr, {} },
            { "comp",    "Compressor",1, false, "compressor", nullptr, {} },
            { "delay",   "Delay",     1, true,  "delay",      "time",  {} },
            { "reverb",  "Reverb",    1, false, "reverb",     nullptr, {} },
            { "pitch",   "Pitch",     1, true,  "pitchshift", "semis", {} },
            { "granular","Granular",  1, false, "granular",   nullptr, {} },
            { "sustain", "Sustain",   1, false, "sustain",    nullptr, {} },
        };
        return defs;
    }

    static const NodeDef* findDef (const juce::String& type)
    {
        for (const auto& d : nodeDefs())
            if (type == d.type)
                return &d;
        return nullptr;
    }

    //==========================================================================
    struct Node
    {
        int id = 0;
        juce::String type;
        float x = 0, y = 0;
        const NodeDef* def = nullptr;

        // primitive params (id -> value)
        std::map<juce::String, double> values;

        // runtime
        std::unique_ptr<Pedal> pedal;   // for pedal-wrapping nodes
        juce::AudioBuffer<double> out;  // stereo output of this node
        SVF svf;
        dsp::StereoBiquad biquad;
        dsp::EnvFollower env;
        dsp::LFO lfo;
        double lastEq = -1.0e9;
        double basePedalModValue = 0.0;

        double value (const juce::String& pid, double fallback) const
        {
            auto it = values.find (pid);
            return it != values.end() ? it->second : fallback;
        }
    };

    struct Connection { int srcNode = 0, dstNode = 0, dstPort = 0; };

    std::vector<std::unique_ptr<Node>> nodes;
    std::vector<Connection> connections;
    int nextId = 1;

    //==========================================================================
    Node* addNode (const juce::String& type, float x, float y)
    {
        const auto* def = findDef (type);
        if (def == nullptr)
            return nullptr;
        auto n = std::make_unique<Node>();
        n->id = nextId++;
        n->type = type;
        n->def = def;
        n->x = x; n->y = y;
        for (const auto& pd : def->params)
            n->values[pd.id] = pd.def;
        if (def->pedalType != nullptr)
            n->pedal = PedalFactory::instance().create (def->pedalType);
        Node* raw = n.get();
        nodes.push_back (std::move (n));
        return raw;
    }

    void removeNode (int id)
    {
        connections.erase (std::remove_if (connections.begin(), connections.end(),
            [id] (const Connection& c) { return c.srcNode == id || c.dstNode == id; }), connections.end());
        nodes.erase (std::remove_if (nodes.begin(), nodes.end(),
            [id] (const std::unique_ptr<Node>& n) { return n->id == id; }), nodes.end());
    }

    Node* findNode (int id) const
    {
        for (const auto& n : nodes)
            if (n->id == id)
                return n.get();
        return nullptr;
    }

    void connect (int src, int dst, int dstPort)
    {
        if (src == dst || findNode (src) == nullptr || findNode (dst) == nullptr)
            return;
        // one connection per input port: replace existing
        disconnectInput (dst, dstPort);
        connections.push_back ({ src, dst, dstPort });
    }

    void disconnectInput (int dst, int dstPort)
    {
        connections.erase (std::remove_if (connections.begin(), connections.end(),
            [&] (const Connection& c) { return c.dstNode == dst && c.dstPort == dstPort; }), connections.end());
    }

    //==========================================================================
    void prepare (double sampleRate, int maxBlock)
    {
        sr = sampleRate;
        blockSize = maxBlock;
        for (auto& n : nodes)
        {
            n->out.setSize (2, maxBlock, false, false, true);
            n->out.clear();
            n->svf.prepare (sampleRate);
            n->biquad.prepare (sampleRate, maxBlock);
            n->env.prepare (sampleRate, 8.0, 150.0);
            n->lfo.prepare (sampleRate);
            n->lastEq = -1.0e9;
            if (n->pedal != nullptr)
            {
                n->pedal->prepareToPlay (sampleRate, maxBlock);
                if (n->def->modParam != nullptr)
                    if (auto* p = n->pedal->param (n->def->modParam))
                        n->basePedalModValue = p->get();
            }
        }
        computeOrder();
        scratch.setSize (2, maxBlock, false, false, true);
        modScratch.allocate ((size_t) maxBlock, true);
    }

    // Kahn topological sort; cyclic remainder keeps insertion order (=> one-block feedback)
    void computeOrder()
    {
        order.clear();
        std::map<int, int> inDeg;
        for (const auto& n : nodes) inDeg[n->id] = 0;
        for (const auto& c : connections) ++inDeg[c.dstNode];

        std::vector<int> ready;
        for (const auto& n : nodes)
            if (inDeg[n->id] == 0)
                ready.push_back (n->id);

        std::set<int> placed;
        while (! ready.empty())
        {
            const int id = ready.front();
            ready.erase (ready.begin());
            order.push_back (id);
            placed.insert (id);
            for (const auto& c : connections)
                if (c.srcNode == id && --inDeg[c.dstNode] == 0)
                    ready.push_back (c.dstNode);
        }
        for (const auto& n : nodes)
            if (placed.find (n->id) == placed.end())
                order.push_back (n->id); // cycle members
    }

    //==========================================================================
    void process (juce::AudioBuffer<double>& io)
    {
        const int n = io.getNumSamples();
        Node* outputNode = nullptr;

        for (int id : order)
        {
            auto* node = findNode (id);
            if (node == nullptr)
                continue;

            // gather audio input (sum of connections to port 0)
            scratch.clear (0, n); scratch.clear (1, n);
            bool anyIn = false;
            for (const auto& c : connections)
            {
                if (c.dstNode != id || c.dstPort != 0)
                    continue;
                if (auto* src = findNode (c.srcNode))
                {
                    for (int ch = 0; ch < 2; ++ch)
                        scratch.addFrom (ch, 0, src->out, ch, 0, n);
                    anyIn = true;
                }
            }

            // gather mod input (mono average of connections to port 1)
            bool anyMod = false;
            std::fill (modScratch.getData(), modScratch.getData() + n, 0.0);
            for (const auto& c : connections)
            {
                if (c.dstNode != id || c.dstPort != 1)
                    continue;
                if (auto* src = findNode (c.srcNode))
                {
                    for (int i = 0; i < n; ++i)
                        modScratch[i] += (src->out.getSample (0, i) + src->out.getSample (1, i)) * 0.5;
                    anyMod = true;
                }
            }

            processNode (*node, n, anyIn, anyMod);
            if (node->type == "output")
                outputNode = node;
        }

        if (outputNode != nullptr)
            for (int ch = 0; ch < 2; ++ch)
                io.copyFrom (ch, 0, outputNode->out, ch, 0, n);
        else
            io.clear();

        graphInput = nullptr;
    }

    void setInput (const juce::AudioBuffer<double>& in) { graphInput = &in; }

private:
    void processNode (Node& node, int n, bool anyIn, bool anyMod)
    {
        auto& out = node.out;
        const auto& type = node.type;

        if (type == "input")
        {
            if (graphInput != nullptr)
                for (int ch = 0; ch < 2; ++ch)
                    out.copyFrom (ch, 0, *graphInput, ch, 0, n);
            else
                out.clear (0, n), out.clear (1, n);
            return;
        }

        // default: start from gathered input
        for (int ch = 0; ch < 2; ++ch)
            out.copyFrom (ch, 0, scratch, ch, 0, n);
        juce::ignoreUnused (anyIn);

        if (type == "output" || type == "splitter")
            return;

        if (type == "gain")
        {
            const double base = juce::Decibels::decibelsToGain (node.value ("gain", 0.0));
            const double modAmt = node.value ("modamt", 0.5);
            for (int ch = 0; ch < 2; ++ch)
            {
                auto* d = out.getWritePointer (ch);
                for (int i = 0; i < n; ++i)
                {
                    const double m = anyMod ? juce::jmax (0.0, 1.0 + modScratch[i] * modAmt) : 1.0;
                    d[i] *= base * m;
                }
            }
        }
        else if (type == "mixer")
        {
            // port 0 = A (already gathered); port 1 here is a SECOND AUDIO input for mixer
            const double blend = node.value ("blend", 50.0) / 100.0;
            for (int ch = 0; ch < 2; ++ch)
            {
                auto* d = out.getWritePointer (ch);
                for (int i = 0; i < n; ++i)
                    d[i] = d[i] * (1.0 - blend) + (anyMod ? modScratch[i] : 0.0) * blend;
            }
        }
        else if (type == "filter")
        {
            const double baseF = node.value ("freq", 800.0);
            const double reso = node.value ("reso", 1.0);
            const int ft = (int) node.value ("ftype", 0.0);
            const double modAmt = node.value ("modamt", 0.5);
            for (int i = 0; i < n; ++i)
            {
                const double f = anyMod ? baseF * std::pow (2.0, modScratch[i] * modAmt * 4.0) : baseF;
                node.svf.set (f, reso);
                for (int ch = 0; ch < 2; ++ch)
                {
                    double lo, bp, hi;
                    node.svf.processSample (ch, out.getSample (ch, i), lo, bp, hi);
                    out.setSample (ch, i, ft == 0 ? lo : ft == 1 ? bp * 1.4 : hi);
                }
            }
        }
        else if (type == "eqband")
        {
            const double key = node.value ("freq", 800.0) * 1.0 + node.value ("gaindb", 0.0) * 1000.0 + node.value ("q", 1.0) * 31.0;
            if (std::abs (key - node.lastEq) > 1.0e-9)
            {
                node.lastEq = key;
                node.biquad.setCoefficients (juce::dsp::IIR::Coefficients<double>::makePeakFilter (
                    sr, juce::jlimit (30.0, sr * 0.45, node.value ("freq", 800.0)),
                    juce::jmax (0.2, node.value ("q", 1.0)),
                    juce::Decibels::decibelsToGain (node.value ("gaindb", 0.0))));
            }
            juce::AudioBuffer<double> view (out.getArrayOfWritePointers(), 2, n);
            node.biquad.processBuffer (view);
        }
        else if (type == "lfo")
        {
            node.lfo.setRate (node.value ("rate", 1.0));
            const int shape = (int) node.value ("shape", 0.0);
            for (int i = 0; i < n; ++i)
            {
                const double v = node.lfo.next (shape);
                out.setSample (0, i, v);
                out.setSample (1, i, v);
            }
        }
        else if (type == "env")
        {
            for (int i = 0; i < n; ++i)
            {
                const double e = node.env.processSample ((out.getSample (0, i) + out.getSample (1, i)) * 0.5);
                out.setSample (0, i, e * 4.0);
                out.setSample (1, i, e * 4.0);
            }
        }
        else if (type == "osc")
        {
            node.lfo.setRate (node.value ("freq", 110.0));
            const int shape = (int) node.value ("shape", 0.0);
            const double level = node.value ("level", 0.3);
            for (int i = 0; i < n; ++i)
            {
                const double v = node.lfo.next (shape) * level;
                out.setSample (0, i, v);
                out.setSample (1, i, v);
            }
        }
        else if (type == "stereo")
        {
            const double w = node.value ("width", 100.0) / 100.0;
            for (int i = 0; i < n; ++i)
            {
                const double mid = (out.getSample (0, i) + out.getSample (1, i)) * 0.5;
                const double side = (out.getSample (0, i) - out.getSample (1, i)) * 0.5 * w;
                out.setSample (0, i, mid + side);
                out.setSample (1, i, mid - side);
            }
        }
        else if (type == "feedback")
        {
            // reads arrive via normal connections (previous block for cycles);
            // this node just attenuates + saturates to keep loops stable
            const double amt = node.value ("amount", 30.0) / 100.0;
            for (int ch = 0; ch < 2; ++ch)
            {
                auto* d = out.getWritePointer (ch);
                for (int i = 0; i < n; ++i)
                    d[i] = std::tanh (d[i] * amt * 1.2);
            }
        }
        else if (node.pedal != nullptr)
        {
            if (anyMod && node.def->modParam != nullptr)
            {
                if (auto* p = node.pedal->param (node.def->modParam))
                {
                    double avg = 0.0;
                    for (int i = 0; i < n; ++i) avg += modScratch[i];
                    avg /= juce::jmax (1, n);
                    const double range = p->def.max - p->def.min;
                    p->set (node.basePedalModValue + avg * range * 0.5);
                }
            }
            juce::AudioBuffer<double> view (out.getArrayOfWritePointers(), 2, n);
            node.pedal->processBlock (view);
        }
    }

public:
    //==========================================================================
    juce::var toVar() const
    {
        auto* obj = new juce::DynamicObject();
        juce::Array<juce::var> ns, cs;
        for (const auto& n : nodes)
        {
            auto* no = new juce::DynamicObject();
            no->setProperty ("id", n->id);
            no->setProperty ("type", n->type);
            no->setProperty ("x", n->x);
            no->setProperty ("y", n->y);
            auto* vals = new juce::DynamicObject();
            for (const auto& [k, v] : n->values)
                vals->setProperty (juce::Identifier (k), v);
            no->setProperty ("values", juce::var (vals));
            if (n->pedal != nullptr)
                no->setProperty ("pedal", n->pedal->toVar());
            ns.add (juce::var (no));
        }
        for (const auto& c : connections)
        {
            auto* co = new juce::DynamicObject();
            co->setProperty ("src", c.srcNode);
            co->setProperty ("dst", c.dstNode);
            co->setProperty ("port", c.dstPort);
            cs.add (juce::var (co));
        }
        obj->setProperty ("nodes", ns);
        obj->setProperty ("connections", cs);
        obj->setProperty ("nextId", nextId);
        return juce::var (obj);
    }

    void fromVar (const juce::var& v)
    {
        nodes.clear();
        connections.clear();
        nextId = (int) v.getProperty ("nextId", 1);
        if (auto* ns = v.getProperty ("nodes", juce::var()).getArray())
        {
            for (const auto& nv : *ns)
            {
                auto* node = addNode (nv.getProperty ("type", "").toString(),
                                      (float) (double) nv.getProperty ("x", 0.0),
                                      (float) (double) nv.getProperty ("y", 0.0));
                if (node == nullptr)
                    continue;
                node->id = (int) nv.getProperty ("id", node->id);
                if (auto* vals = nv.getProperty ("values", juce::var()).getDynamicObject())
                    for (const auto& kv : vals->getProperties())
                        node->values[kv.name.toString()] = (double) kv.value;
                if (node->pedal != nullptr && nv.hasProperty ("pedal"))
                    node->pedal->fromVar (nv.getProperty ("pedal", juce::var()));
            }
            // restore nextId correctness
            for (const auto& n : nodes)
                nextId = juce::jmax (nextId, n->id + 1);
        }
        if (auto* cs = v.getProperty ("connections", juce::var()).getArray())
            for (const auto& cv : *cs)
                connections.push_back ({ (int) cv.getProperty ("src", 0),
                                         (int) cv.getProperty ("dst", 0),
                                         (int) cv.getProperty ("port", 0) });
    }

    double sr = 48000.0;
    int blockSize = 512;

private:
    std::vector<int> order;
    juce::AudioBuffer<double> scratch;
    juce::HeapBlock<double> modScratch;
    const juce::AudioBuffer<double>* graphInput = nullptr;
};

//==============================================================================
// A pedal whose DSP is a user-built node graph.
class CustomPedal : public Pedal
{
public:
    CustomPedal (const juce::String& name, const juce::var& graphVar)
        : Pedal ("custom", name, "Custom", true)
    {
        graph.fromVar (graphVar);
        savedGraph = graphVar;
    }

    void prepare (double sampleRate, int maxBlockSize) override
    {
        graph.prepare (sampleRate, maxBlockSize);
        inCopy.setSize (2, maxBlockSize, false, false, true);
    }

    void process (juce::AudioBuffer<double>& buf) override
    {
        const int n = buf.getNumSamples();
        for (int ch = 0; ch < 2; ++ch)
            inCopy.copyFrom (ch, 0, buf, ch, 0, n);
        graph.setInput (inCopy);
        graph.process (buf);
    }

    juce::var toVar() const override
    {
        auto v = Pedal::toVar();
        if (auto* obj = v.getDynamicObject())
        {
            obj->setProperty ("graph", savedGraph);
            obj->setProperty ("customName", displayName);
        }
        return v;
    }

private:
    NodeGraph graph;
    juce::var savedGraph;
    juce::AudioBuffer<double> inCopy;
};

//==============================================================================
// Disk library of user-built pedals (.vpnode JSON files).
struct NodeGraphLibrary
{
    static juce::File dir()
    {
        auto d = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                     .getChildFile ("VirtualPedals").getChildFile ("CustomPedals");
        d.createDirectory();
        return d;
    }

    static juce::Array<juce::File> listCustomPedals()
    {
        return dir().findChildFiles (juce::File::findFiles, false, "*.vpnode");
    }

    static void save (const juce::String& name, const juce::var& graphVar)
    {
        dir().getChildFile (juce::File::createLegalFileName (name) + ".vpnode")
             .replaceWithText (juce::JSON::toString (graphVar, false));
    }

    static std::unique_ptr<Pedal> createCustomPedal (const juce::File& file)
    {
        const auto v = juce::JSON::parse (file.loadFileAsString());
        if (v.isVoid())
            return nullptr;
        return std::make_unique<CustomPedal> (file.getFileNameWithoutExtension(), v);
    }
};

} // namespace vp
