#include "TestHarness.h"
#include "AudioEngine/ExportAudioProcessing.h"
#include "AudioEngine/ExportFileSupport.h"
#include "AudioEngine/ExportFormatSupport.h"
#include <cstring>

namespace {

bool canCreateAndWrite(const juce::String &formatName,
                       const juce::AudioFormatWriterOptions &options,
                       const char *expectedSignature) {
  juce::AudioFormatManager manager;
  manager.registerBasicFormats();
  auto *format = findExportAudioFormat(manager, formatName);
  if (format == nullptr)
    return false;

  juce::MemoryBlock encodedData;
  std::unique_ptr<juce::OutputStream> stream =
      std::make_unique<juce::MemoryOutputStream>(encodedData, false);
  auto writer = format->createWriterFor(stream, options);
  if (writer == nullptr)
    return false;

  juce::AudioBuffer<float> buffer(2, 512);
  buffer.clear();
  if (!writer->writeFromAudioSampleBuffer(buffer, 0, buffer.getNumSamples()))
    return false;

  writer.reset();
  const auto signatureLength = std::strlen(expectedSignature);
  return encodedData.getSize() >= signatureLength &&
         std::memcmp(encodedData.getData(), expectedSignature,
                     signatureLength) == 0;
}

} // namespace

namespace miditest {

int runExportEncodingTests() {
  int failures = 0;
  const auto expect = [&](bool condition, const char *message) {
    miditest::expect(failures, condition, message);
  };

  const auto ogg = getExportFormatCapabilities("Ogg Vorbis");
  expect(ogg.available, "Ogg Vorbis should be available");
  expect(ogg.bitDepths.contains(32), "Ogg Vorbis should use 32-bit input");
  expect(!ogg.bitDepths.contains(16),
         "Ogg Vorbis should not advertise 16-bit input");
  expect(ogg.sampleRates.contains(96000),
         "Ogg Vorbis should allow 96 kHz");
  expect(ogg.qualityOptions.size() == 11,
         "Ogg Vorbis should expose all encoder bitrates");

  const auto validOgg =
      validateExportFormatSettings("Ogg Vorbis", 96000.0, 32, true, 10);
  expect(validOgg.wasOk(), "valid Ogg settings should pass validation");

  const auto invalidOgg =
      validateExportFormatSettings("Ogg Vorbis", 48000.0, 16, false, 5);
  expect(invalidOgg.failed(), "16-bit Ogg settings should be rejected");

  const auto wavFloat =
      createExportWriterOptions(96000.0, 32, true, 0, "Float Export");
  expect(wavFloat.getSampleFormat() ==
             juce::AudioFormatWriterOptions::SampleFormat::floatingPoint,
         "32-bit WAV float should request IEEE floating-point samples");
  expect(canCreateAndWrite("WAV", wavFloat, "RIFF"),
         "32-bit WAV float writer should encode a RIFF file");

  const auto flacInteger =
      createExportWriterOptions(96000.0, 24, false, 5, "FLAC Export");
  expect(canCreateAndWrite("FLAC", flacInteger, "fLaC"),
         "24-bit FLAC writer should encode a FLAC file");

  const auto oggFloat =
      createExportWriterOptions(96000.0, 32, true, 6, "Ogg Export");
  expect(canCreateAndWrite("Ogg Vorbis", oggFloat, "OggS"),
         "Ogg Vorbis writer should encode an Ogg stream");
  auto tempDir = juce::File::getSpecialLocation(
                     juce::File::tempDirectory)
                     .getNonexistentChildFile("midi-player-export-tests", "");
  expect(tempDir.createDirectory(), "temporary test directory should exist");
  const auto chosenFile = tempDir.getChildFile("mix.wav");
  const auto normalisedFile = normaliseExportTargetFile(chosenFile, "FLAC");
  expect(normalisedFile.hasFileExtension(".flac"),
         "export target should use the selected format extension");
  normalisedFile.replaceWithText("existing");
  expect(exportTargetNeedsOverwriteConfirmation(normalisedFile),
         "overwrite confirmation should inspect the normalised target");

  juce::AudioBuffer<float> floatBuffer(2, 1);
  floatBuffer.setSample(0, 0, 2.0f);
  floatBuffer.setSample(1, 0, -2.0f);
  applyMasterOutputStage(
      floatBuffer, 0.8f,
      shouldPreserveExportHeadroom("WAV", 32, true));
  expect(floatBuffer.getSample(0, 0) > 1.5f,
         "32-bit float WAV should preserve samples above 0 dBFS");

  juce::AudioBuffer<float> integerBuffer(2, 1);
  integerBuffer.setSample(0, 0, 2.0f);
  integerBuffer.setSample(1, 0, -2.0f);
  applyMasterOutputStage(
      integerBuffer, 0.8f,
      shouldPreserveExportHeadroom("WAV", 16, false));
  expect(integerBuffer.getMagnitude(0, 1) == 1.0f,
         "integer export should clip only values outside the representable range");

  expect(shouldDitherExport("WAV", 16, false),
         "16-bit WAV should use TPDF dither");
  expect(shouldDitherExport("FLAC", 24, false),
         "24-bit FLAC should use TPDF dither");
  expect(!shouldDitherExport("WAV", 32, true),
         "32-bit float WAV should not use dither");
  expect(!shouldDitherExport("Ogg Vorbis", 32, true),
         "Ogg Vorbis input should not use PCM dither");

  juce::AudioBuffer<float> ditherBuffer(2, 256);
  ditherBuffer.clear();
  expect(measureExportTailLevel(ditherBuffer) == 0.0f,
         "tail detection should see digital silence before dither");
  juce::Random ditherRandom(12345);
  applyTpdfDither(ditherBuffer, 16, ditherRandom);
  expect(ditherBuffer.getMagnitude(0, ditherBuffer.getNumSamples()) > 0.0f,
         "TPDF dither should decorrelate quantisation from digital silence");
  expect(measureExportTailLevel(ditherBuffer) > 0.00001f,
         "16-bit dither must not be included in automatic tail detection");

  tempDir.deleteRecursively();

  return failures;
}

} // namespace miditest
