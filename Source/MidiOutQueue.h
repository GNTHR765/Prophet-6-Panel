#pragma once
#include <juce_audio_devices/juce_audio_devices.h>
#include <atomic>
#include <functional>
#include <vector>
#include <cstring>
#include <cmath>

/** A MIDI output that never makes its caller wait.

    Everything sent to it is queued and handed to the device by a small thread of its own, so neither the message thread
    (the knobs, the timers, and with them the plugin window) nor the audio thread can be held up by a slow or stalled
    MIDI port - a USB device that hangs, a hub that resets. It is written to stand in for a
    std::unique_ptr<juce::MidiOutput>: assign an opened device to it, test it against nullptr, reset() it, and send through
    out->sendMessageNow() / out->sendBlockOfMessagesNow() as before. The device itself is owned and used by the thread alone.

    Shared by the Claude panels (Prophet 6, Rev2, Voyager, alpha-Juno, TX-802, TR-8S Studio). */
class MidiOutQueue : private juce::Thread
{
public:
    explicit MidiOutQueue (const juce::String& threadName = "Panel MIDI out") : juce::Thread (threadName)
    {
        ring.resize ((size_t) fifo.getTotalSize());
        startThread (Priority::high);
    }
    ~MidiOutQueue() override { signalThreadShouldExit(); wake.signal(); stopThread (3000); }

    // ---- standing in for std::unique_ptr<juce::MidiOutput>
    MidiOutQueue& operator= (std::unique_ptr<juce::MidiOutput> device) { const bool has = device != nullptr; push (Item { {}, std::move (device), true }); open = has; return *this; }
    MidiOutQueue& operator= (std::nullptr_t) { reset(); return *this; }
    void reset() { open = false; push (Item { {}, nullptr, true }); }
    bool operator== (std::nullptr_t) const noexcept { return ! open.load(); }
    bool operator!= (std::nullptr_t) const noexcept { return open.load(); }
    explicit operator bool() const noexcept { return open.load(); }
    MidiOutQueue* operator->() noexcept { return this; }

    // ---- sending: safe from any thread, returns at once
    void sendMessageNow (const juce::MidiMessage& m) { if (open.load()) push (Item { m, nullptr, false }); }
    /** One message with its time (Time::getMillisecondCounterHiRes() scale): it leaves when that moment has come. For the audio
        thread - a sequencer's notes, each at its own place inside the block instead of all at the block's start. The queue keeps
        its order, so what is sent after it waits behind it (at most a block's length). */
    void sendMessageAt (const juce::MidiMessage& m, double dueMs) { if (! open.load()) return; Item it { m, nullptr, false }; it.dueMs = dueMs; push (std::move (it)); }
    std::function<void (const juce::MidiMessage&)> sentTap;   // for the test harness: every message at the moment it leaves the queue (on the queue's thread)
    void openForTest() { open = true; }
    /** A block as one piece: its messages go out together, with nothing from another thread between them (an NRPN's four
        controllers, a 14-bit pair). It is dropped whole if the queue cannot take all of it. */
    void sendBlockOfMessagesNow (const juce::MidiBuffer& b)
    {
        if (! open.load()) return;
        Item items[kGroupMax]; int n = 0;
        for (const auto meta : b) { if (n == kGroupMax) { pushGroup (items, n); n = 0; } items[n++] = Item { meta.getMessage(), nullptr, false }; }
        if (n > 0) pushGroup (items, n);
    }
    /** A block with its timing, as juce::MidiOutput does it: each message goes out at msCounterToStartAt + its sample position
        (Time::getMillisecondCounterHiRes() scale). The queue keeps its order, so everything sent after this block - from any
        thread - waits behind it (at most the block's length). */
    void sendBlockOfMessages (const juce::MidiBuffer& b, double msCounterToStartAt, double sampleRate)
    {
        if (! open.load()) return;
        Item items[kGroupMax]; int n = 0;
        for (const auto meta : b)
        {
            if (n == kGroupMax) { pushGroup (items, n); n = 0; }
            items[n] = Item { meta.getMessage(), nullptr, false }; items[n].dueMs = msCounterToStartAt + (sampleRate > 0 ? 1000.0 * meta.samplePosition / sampleRate : 0.0); ++n;
        }
        if (n > 0) pushGroup (items, n);
    }
    /** Up to kGroupMax short messages as one piece, without a MidiBuffer (whose storage is on the heap): for the audio thread. */
    void sendGroupNow (const juce::MidiMessage* msgs, int n)
    {
        if (! open.load() || n <= 0) return;
        Item items[kGroupMax]; int k = 0;
        for (int i = 0; i < n; ++i) { if (k == kGroupMax) { pushGroup (items, k); k = 0; } items[k++] = Item { msgs[i], nullptr, false }; }
        if (k > 0) pushGroup (items, k);
    }
    void startBackgroundThread() {}                                                                                                            // it always has one
    /** Raw bytes (a complete MIDI message, F0..F7 for SysEx, up to kRawMax bytes) without making a juce::MidiMessage first -
        a MidiMessage longer than a few bytes lives on the heap, which the audio thread must not touch. The message is built
        on the queue's own thread. */
    static constexpr int kRawMax = 24;
    void sendRawFromAudio (const juce::uint8* bytes, int numBytes)
    {
        if (! open.load() || numBytes <= 0 || numBytes > kRawMax) return;
        Item it; it.rawLen = numBytes; std::memcpy (it.raw, bytes, (size_t) numBytes); push (std::move (it));
    }

private:
    static constexpr int kGroupMax = 8;
    struct Item { juce::MidiMessage msg; std::unique_ptr<juce::MidiOutput> device; bool replace = false; juce::uint8 raw[kRawMax] {}; int rawLen = 0; double dueMs = 0.0; };   // dueMs 0: at once

    void push (Item&& item)
    {
        for (int attempt = 0;; ++attempt)
        {
            {
                const juce::SpinLock::ScopedLockType sl (pushLock);
                int s1, n1, s2, n2; fifo.prepareToWrite (1, s1, n1, s2, n2);
                if (n1 + n2 >= 1) { ring[(size_t) s1] = std::move (item); fifo.finishedWrite (1); break; }
            }
            // full (4096 messages the device has not taken): a note or a value is dropped rather than waited for;
            // a device change is worth a short wait, so the panel never ends up talking to the wrong port
            if (! item.replace || attempt > 200) return;
            juce::Thread::sleep (2);
        }
        wake.signal();
    }
    void pushGroup (Item* items, int n)
    {
        {
            const juce::SpinLock::ScopedLockType sl (pushLock);
            int s1, n1, s2, n2; fifo.prepareToWrite (n, s1, n1, s2, n2);
            if (n1 + n2 < n) return;   // not room for the whole group: none of it (half an NRPN would set the wrong parameter)
            for (int i = 0; i < n; ++i) ring[(size_t) (i < n1 ? s1 + i : s2 + i - n1)] = std::move (items[i]);
            fifo.finishedWrite (n);
        }
        wake.signal();
    }
    void sendOne (Item& it)
    {
        if (it.replace) device = std::move (it.device);
        else
        {
            if (sentTap) sentTap (it.rawLen > 0 ? juce::MidiMessage (it.raw, it.rawLen) : it.msg);
            if (device != nullptr) { if (it.rawLen > 0) device->sendMessageNow (juce::MidiMessage (it.raw, it.rawLen)); else device->sendMessageNow (it.msg); }
        }
        it = Item();
    }
    /** Sends what is ready; a timed item that is not yet due stops it, and the wait until then is returned (0: nothing waits). */
    double drain (bool honourTiming)
    {
        while (fifo.getNumReady() > 0 && ! (honourTiming && threadShouldExit()))
        {
            int s1, n1, s2, n2; fifo.prepareToRead (1, s1, n1, s2, n2);
            if (n1 + n2 < 1) break;
            auto& it = ring[(size_t) s1];
            if (honourTiming && it.dueMs > 0.0) { const double wait = it.dueMs - juce::Time::getMillisecondCounterHiRes(); if (wait > 0.05) return wait; }
            sendOne (it);
            fifo.finishedRead (1);
        }
        return 0.0;
    }
    void run() override
    {
        while (! threadShouldExit())
        {
            // a timed message: sleep through the whole milliseconds, then wait the last fraction out here - a sleep is only as
            // fine as a millisecond, and a note that is a millisecond late is heard in a fast pattern
            const double wait = drain (true);
            if (wait <= 0.0) wake.wait (250);
            else if (wait >= 1.0) wake.wait (juce::jmin (250, (int) wait));
            else juce::Thread::yield();
        }
        drain (false);   // the plugin is going: what it queued on its way out (note-offs, restored values) still reaches the synth
        device.reset();
    }

    std::vector<Item> ring; juce::AbstractFifo fifo { 4096 }; juce::SpinLock pushLock; juce::WaitableEvent wake;
    std::unique_ptr<juce::MidiOutput> device;   // the thread's
    std::atomic<bool> open { false };          // the callers' view: a device has been handed over
};

/** Tells a panel when the Mac's list of MIDI devices has changed, so it asks the MIDI server for the list only then -
    asking for it is what can hold the message thread up for seconds while a USB device misbehaves.
    Create and poll on the message thread. version() goes up by one with every change. */
class MidiDeviceWatch
{
public:
    int version()
    {
        if (! started) { started = true; conn = juce::MidiDeviceListConnection::make ([this] { ++count; }); }
        return count;
    }
    /** True when the list changed since the last call (and, as a safety net, every `everyN`-th call anyway). */
    bool changedSinceLastAsk (int everyN = 8)
    {
        const int v = version();
        const bool c = v != seen || ++calls % everyN == 0 || first;
        seen = v; first = false; return c;
    }
private:
    juce::MidiDeviceListConnection conn; bool started = false, first = true; int count = 0, seen = 0, calls = 0;
};
