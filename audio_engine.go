package main

/*
#cgo CXXFLAGS: -std=c++11
#cgo windows LDFLAGS: -lole32 -lwinmm -lm
#cgo darwin LDFLAGS: -framework CoreAudio -framework CoreFoundation -lm
#cgo linux LDFLAGS: -lasound -lpthread -lm -ldl
#include "audio_engine.h"
#include <stdlib.h>
*/
import "C"
import (
	"unsafe"
)

type TrackMetadata struct {
	FilePath     string    `json:"filePath"`
	DurationSec  float64   `json:"durationSec"`
	BPM          float64   `json:"bpm"`
	KeySignature string    `json:"keySignature"`
	Waveform     []float32 `json:"waveform"`
}

func InitAudioEngine(sampleRate int, channels int) bool {
	res := C.init_audio_engine(C.int(sampleRate), C.int(channels))
	return res != 0
}

func CleanupAudioEngine() {
	C.cleanup_audio_engine()
}

func LoadTrack(slot int, filePath string) bool {
	cPath := C.CString(filePath)
	defer C.free(unsafe.Pointer(cPath))
	res := C.load_track(C.int(slot), cPath)
	return res != 0
}

func GetTrackMetadata(slot int, filePath string) TrackMetadata {
	metaC := C.get_track_metadata(C.int(slot))
	defer C.free_track_metadata(metaC)

	var waveform []float32
	if metaC.waveformSize > 0 && metaC.waveformData != nil {
		waveformSlice := (*[1 << 28]float32)(unsafe.Pointer(metaC.waveformData))[:metaC.waveformSize:metaC.waveformSize]
		waveform = make([]float32, metaC.waveformSize)
		copy(waveform, waveformSlice)
	}

	key := ""
	if metaC.keySignature != nil {
		key = C.GoString(metaC.keySignature)
	}

	return TrackMetadata{
		FilePath:     filePath,
		DurationSec:  float64(metaC.durationSec),
		BPM:          float64(metaC.bpm),
		KeySignature: key,
		Waveform:     waveform,
	}
}

func PlayTrack(slot int) {
	C.play_track(C.int(slot))
}

func PauseTrack(slot int) {
	C.pause_track(C.int(slot))
}

func SeekTrack(slot int, positionSec float64) {
	C.seek_track(C.int(slot), C.double(positionSec))
}

func SetTrackVolume(slot int, volume float32) {
	C.set_track_volume(C.int(slot), C.float(volume))
}

func SetTrackTempo(slot int, tempoRatio float64) {
	C.set_track_tempo(C.int(slot), C.double(tempoRatio))
}

func SetTrackPitch(slot int, pitchSemi float64) {
	C.set_track_pitch(C.int(slot), C.double(pitchSemi))
}

func GetTrackPosition(slot int) float64 {
	return float64(C.get_track_position(C.int(slot)))
}

func IsTrackPlaying(slot int) bool {
	return C.is_track_playing(C.int(slot)) != 0
}

func SetAutomixEnabled(enabled bool) {
	val := 0
	if enabled {
		val = 1
	}
	C.set_automix_enabled(C.int(val))
}
