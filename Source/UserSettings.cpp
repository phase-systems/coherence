#include "UserSettings.h"

#include <atomic>

namespace coherence
{
namespace
{
const juce::String gainSteppingKey { "gainStepping" };
const juce::String defaultScaleKey { "defaultScale" };
const juce::String themeKey { "theme" };

juce::File& testFile()
{
    static juce::File f;
    return f;
}

std::atomic<float> currentStep { 0.1f };
} // namespace

float currentGainStepDb() noexcept { return currentStep.load (std::memory_order_relaxed); }

float gainStepDb (GainStepping g)
{
    switch (g)
    {
        case GainStepping::standard:  return 0.5f;
        case GainStepping::precision: return 0.01f;
        case GainStepping::fine:      break;
    }
    return 0.1f;
}

juce::String gainSteppingLabel (GainStepping g)
{
    switch (g)
    {
        case GainStepping::standard:  return "Standard (0.5 dB)";
        case GainStepping::precision: return "Precision (0.01 dB)";
        case GainStepping::fine:      break;
    }
    return "Fine (0.1 dB)";
}

UserSettings::UserSettings()
{
    juce::PropertiesFile::Options o;
    o.applicationName = "COHERENCE";
    o.filenameSuffix = "settings";
    o.folderName = "Phase Systems";
    o.osxLibrarySubFolder = "Application Support";
    o.storageFormat = juce::PropertiesFile::storeAsXML;
    o.millisecondsBeforeSaving = 0;   // write on every change
    o.processLock = nullptr;
    const auto f = testFile() != juce::File() ? testFile() : o.getDefaultFile();
    props = std::make_unique<juce::PropertiesFile> (f, o);
    currentStep = gainStepDb (gainStepping());
}

UserSettings::~UserSettings()
{
    props->saveIfNeeded();
}

void UserSettings::setFileForTesting (const juce::File& f) { testFile() = f; }
juce::File UserSettings::file() const { return props->getFile(); }

GainStepping UserSettings::gainStepping() const
{
    const int v = props->getIntValue (gainSteppingKey, (int) GainStepping::fine);
    return (v >= 0 && v <= 2) ? (GainStepping) v : GainStepping::fine;   // anything unknown falls back to the default
}

void UserSettings::setGainStepping (GainStepping g)
{
    if (g == gainStepping())
        return;
    props->setValue (gainSteppingKey, (int) g);
    props->saveIfNeeded();
    currentStep = gainStepDb (g);
    sendChangeMessage();
}
float UserSettings::defaultScale() const
{
    const double v = props->getDoubleValue (defaultScaleKey, 1.0);
    return (v >= 0.6 && v <= 2.0) ? (float) v : 1.0f;
}

juce::String UserSettings::themeId() const
{
    return props->getValue (themeKey, "classic");
}

void UserSettings::setThemeId (const juce::String& id)
{
    if (id == themeId())
        return;
    props->setValue (themeKey, id);
    props->saveIfNeeded();
    sendChangeMessage();
}

void UserSettings::setDefaultScale (float s)
{
    if (std::abs (s - defaultScale()) < 1e-4f)
        return;
    props->setValue (defaultScaleKey, (double) s);
    props->saveIfNeeded();
}
} // namespace coherence
