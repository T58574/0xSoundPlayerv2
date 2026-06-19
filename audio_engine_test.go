package main

import (
	"encoding/binary"
	"math"
	"os"
	"path/filepath"
	"testing"
)

func createTestWav(path string) error {
	f, err := os.Create(path)
	if err != nil {
		return err
	}
	defer f.Close()

	sampleRate := 44100
	duration := 1.0
	numSamples := int(float64(sampleRate) * duration)
	numChannels := 2

	writeString(f, "RIFF")
	writeInt32(f, int32(36+numSamples*numChannels*2))
	writeString(f, "WAVE")
	writeString(f, "fmt ")
	writeInt32(f, 16)
	writeInt16(f, 1)
	writeInt16(f, int16(numChannels))
	writeInt32(f, int32(sampleRate))
	writeInt32(f, int32(sampleRate*numChannels*2))
	writeInt16(f, int16(numChannels*2))
	writeInt16(f, 16)
	writeString(f, "data")
	writeInt32(f, int32(numSamples*numChannels*2))

	for i := 0; i < numSamples; i++ {
		t := float64(i) / float64(sampleRate)
		val := int16(math.Sin(2.0*math.Pi*440.0*t) * 32767.0)
		for c := 0; c < numChannels; c++ {
			binary.Write(f, binary.LittleEndian, val)
		}
	}
	return nil
}

func writeString(f *os.File, s string) {
	f.Write([]byte(s))
}

func writeInt32(f *os.File, v int32) {
	binary.Write(f, binary.LittleEndian, v)
}

func writeInt16(f *os.File, v int16) {
	binary.Write(f, binary.LittleEndian, v)
}

func TestAudioEngine(t *testing.T) {
	tmpDir := t.TempDir()
	wavPath := filepath.Join(tmpDir, "test.wav")
	err := createTestWav(wavPath)
	if err != nil {
		t.Fatalf("failed to create test wav: %v", err)
	}

	ok := InitAudioEngine(44100, 2)
	if !ok {
		t.Fatalf("failed to initialize audio engine")
	}
	defer CleanupAudioEngine()

	app := NewApp()
	app.startup(nil)
	defer app.shutdown(nil)

	meta, err := app.LoadTrack(0, wavPath)
	if err != nil {
		t.Fatalf("failed to load track: %v", err)
	}

	if meta.DurationSec <= 0.0 {
		t.Errorf("expected positive duration, got %f", meta.DurationSec)
	}

	if meta.BPM <= 0.0 {
		t.Errorf("expected positive BPM, got %f", meta.BPM)
	}

	if meta.KeySignature == "" {
		t.Errorf("expected non-empty key signature")
	}

	if len(meta.Waveform) == 0 {
		t.Errorf("expected non-empty waveform")
	}

	app.Play(0)
	if !app.IsPlaying(0) {
		t.Errorf("expected track to be playing")
	}

	app.SetVolume(0, 0.5)
	app.SetTempo(0, 1.1)
	app.SetPitch(0, 1.0)
	app.Seek(0, 0.2)

	pos := app.GetPosition(0)
	if pos < 0.0 {
		t.Errorf("expected non-negative position, got %f", pos)
	}

	app.ToggleAutoMix(true)
	app.ToggleAutoMix(false)

	app.Pause(0)
	if app.IsPlaying(0) {
		t.Errorf("expected track to be paused")
	}
}
