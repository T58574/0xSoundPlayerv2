package main

import (
	"context"
	"fmt"
	"github.com/wailsapp/wails/v2/pkg/runtime"
)

type App struct {
	ctx context.Context
}

func NewApp() *App {
	return &App{}
}

func (a *App) startup(ctx context.Context) {
	a.ctx = ctx
	InitAudioEngine(44100, 2)
}

func (a *App) shutdown(ctx context.Context) {
	CleanupAudioEngine()
}

func (a *App) LoadTrack(slot int, filePath string) (TrackMetadata, error) {
	ok := LoadTrack(slot, filePath)
	if !ok {
		return TrackMetadata{}, fmt.Errorf("failed to load track")
	}
	return GetTrackMetadata(slot, filePath), nil
}

func (a *App) Play(slot int) {
	PlayTrack(slot)
}

func (a *App) Pause(slot int) {
	PauseTrack(slot)
}

func (a *App) Seek(slot int, positionSec float64) {
	SeekTrack(slot, positionSec)
}

func (a *App) SetVolume(slot int, volume float32) {
	SetTrackVolume(slot, volume)
}

func (a *App) SetTempo(slot int, tempoRatio float64) {
	SetTrackTempo(slot, tempoRatio)
}

func (a *App) SetPitch(slot int, pitchSemi float64) {
	SetTrackPitch(slot, pitchSemi)
}

func (a *App) GetPosition(slot int) float64 {
	return GetTrackPosition(slot)
}

func (a *App) IsPlaying(slot int) bool {
	return IsTrackPlaying(slot)
}

func (a *App) ToggleAutoMix(enabled bool) {
	SetAutomixEnabled(enabled)
}

func (a *App) SelectAudioFile() (string, error) {
	return runtime.OpenFileDialog(a.ctx, runtime.OpenDialogOptions{
		Title: "Select Audio File",
		Filters: []runtime.FileFilter{
			{
				DisplayName: "Audio Files (*.mp3, *.wav, *.flac)",
				Pattern:     "*.mp3;*.wav;*.flac",
			},
		},
	})
}

