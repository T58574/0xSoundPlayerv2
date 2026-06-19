import { useState, useEffect, useRef } from 'react';
import './App.css';
import {
    LoadTrack,
    Play,
    Pause,
    Seek,
    SetVolume,
    SetTempo,
    SetPitch,
    GetPosition,
    IsPlaying,
    ToggleAutoMix,
    SelectAudioFile
} from "../wailsjs/go/main/App";

interface TrackInfo {
    filePath: string;
    title: string;
    durationSec: number;
    bpm: number;
    keySignature: string;
    waveform: number[];
}

const PlayIcon = () => (
    <svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="currentColor" width="24" height="24">
        <path d="M8 5v14l11-7z" />
    </svg>
);

const PauseIcon = () => (
    <svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="currentColor" width="24" height="24">
        <path d="M6 19h4V5H6v14zm8-14v14h4V5h-4z" />
    </svg>
);

const MusicIcon = () => (
    <svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round" width="32" height="32">
        <path d="M9 18V5l12-2v13" />
        <circle cx="6" cy="18" r="3" />
        <circle cx="18" cy="16" r="3" />
    </svg>
);

const LoadIcon = () => (
    <svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round" width="16" height="16">
        <path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4" />
        <polyline points="17 8 12 3 7 8" />
        <line x1="12" y1="3" x2="12" y2="15" />
    </svg>
);

const LightningIcon = () => (
    <svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="currentColor" width="16" height="16">
        <polygon points="13 2 3 14 12 14 11 22 21 10 12 10 13 2" />
    </svg>
);

const ResetIcon = () => (
    <svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round" width="12" height="12">
        <path d="M3 12a9 9 0 1 0 9-9 9.75 9.75 0 0 0-6.74 2.74L3 8" />
        <polyline points="3 3 3 8 8 8" />
    </svg>
);

function App() {
    const [tracks, setTracks] = useState<[TrackInfo | null, TrackInfo | null]>([null, null]);
    const [playing, setPlaying] = useState<[boolean, boolean]>([false, false]);
    const [positions, setPositions] = useState<[number, number]>([0, 0]);
    const [volumes, setVolumes] = useState<[number, number]>([1.0, 1.0]);
    const [tempos, setTempos] = useState<[number, number]>([1.0, 1.0]);
    const [pitches, setPitches] = useState<[number, number]>([0.0, 0.0]);
    const [autoMix, setAutoMix] = useState<boolean>(false);

    const canvasRef0 = useRef<HTMLCanvasElement | null>(null);
    const canvasRef1 = useRef<HTMLCanvasElement | null>(null);

    const getFilename = (path: string) => {
        if (!path) return '';
        const parts = path.split(/[/\\]/);
        return parts[parts.length - 1];
    };

    const handleLoad = async (slot: number) => {
        try {
            const filePath = await SelectAudioFile();
            if (!filePath) return;
            const meta = await LoadTrack(slot, filePath);
            const title = getFilename(filePath);
            const updated = [...tracks] as [TrackInfo | null, TrackInfo | null];
            updated[slot] = {
                filePath,
                title,
                durationSec: meta.durationSec,
                bpm: meta.bpm,
                keySignature: meta.keySignature,
                waveform: meta.waveform || []
            };
            setTracks(updated);
            
            const updatedPlaying = [...playing] as [boolean, boolean];
            updatedPlaying[slot] = false;
            setPlaying(updatedPlaying);
            
            const updatedPos = [...positions] as [number, number];
            updatedPos[slot] = 0;
            setPositions(updatedPos);
            
            const updatedTempos = [...tempos] as [number, number];
            updatedTempos[slot] = 1.0;
            setTempos(updatedTempos);
            
            const updatedPitches = [...pitches] as [number, number];
            updatedPitches[slot] = 0.0;
            setPitches(updatedPitches);
        } catch (err) {
            console.error(err);
        }
    };

    const handlePlayPause = async (slot: number) => {
        if (!tracks[slot]) return;
        if (playing[slot]) {
            await Pause(slot);
            const updated = [...playing] as [boolean, boolean];
            updated[slot] = false;
            setPlaying(updated);
        } else {
            await Play(slot);
            const updated = [...playing] as [boolean, boolean];
            updated[slot] = true;
            setPlaying(updated);
        }
    };

    const handleVolume = async (slot: number, val: number) => {
        const updated = [...volumes] as [number, number];
        updated[slot] = val;
        setVolumes(updated);
        await SetVolume(slot, val);
    };

    const handleTempo = async (slot: number, val: number) => {
        const updated = [...tempos] as [number, number];
        updated[slot] = val;
        setTempos(updated);
        await SetTempo(slot, val);
    };

    const handlePitch = async (slot: number, val: number) => {
        const updated = [...pitches] as [number, number];
        updated[slot] = val;
        setPitches(updated);
        await SetPitch(slot, val);
    };

    const handleSeek = async (slot: number, pct: number) => {
        const track = tracks[slot];
        if (!track) return;
        const newPos = pct * track.durationSec;
        await Seek(slot, newPos);
        const updated = [...positions] as [number, number];
        updated[slot] = newPos;
        setPositions(updated);
    };

    const handleCanvasClick = (e: React.MouseEvent<HTMLCanvasElement>, slot: number) => {
        const canvas = e.currentTarget;
        const rect = canvas.getBoundingClientRect();
        const clickX = e.clientX - rect.left;
        const pct = clickX / rect.width;
        handleSeek(slot, pct);
    };

    const handleToggleAutoMix = async () => {
        const val = !autoMix;
        setAutoMix(val);
        await ToggleAutoMix(val);
    };

    useEffect(() => {
        const interval = setInterval(async () => {
            const updatedPlaying = [...playing] as [boolean, boolean];
            const updatedPos = [...positions] as [number, number];
            
            for (let slot = 0; slot < 2; slot++) {
                if (tracks[slot]) {
                    const isPlay = await IsPlaying(slot);
                    updatedPlaying[slot] = isPlay;
                    if (isPlay) {
                        const pos = await GetPosition(slot);
                        updatedPos[slot] = pos;
                    }
                }
            }
            
            setPlaying(updatedPlaying);
            setPositions(updatedPos);
        }, 100);
        return () => clearInterval(interval);
    }, [tracks, playing, positions]);

    const draw = (canvas: HTMLCanvasElement, peaks: number[], pos: number, dur: number) => {
        const ctx = canvas.getContext('2d');
        if (!ctx) return;
        const w = canvas.width;
        const h = canvas.height;
        ctx.clearRect(0, 0, w, h);

        if (peaks.length === 0) {
            ctx.strokeStyle = 'rgba(255, 255, 255, 0.08)';
            ctx.lineWidth = 2;
            ctx.beginPath();
            ctx.moveTo(0, h / 2);
            ctx.lineTo(w, h / 2);
            ctx.stroke();
            return;
        }

        const playPct = dur > 0 ? pos / dur : 0;
        const barWidth = w / peaks.length;

        for (let i = 0; i < peaks.length; i++) {
            const pct = i / peaks.length;
            const val = peaks[i];
            const barHeight = val * h * 0.85;
            const x = i * barWidth;
            const y = (h - barHeight) / 2;

            if (pct < playPct) {
                ctx.fillStyle = '#22C55E';
            } else {
                ctx.fillStyle = 'rgba(255, 255, 255, 0.12)';
            }
            ctx.fillRect(x, y, Math.max(1, barWidth - 1), Math.max(2, barHeight));
        }
    };

    useEffect(() => {
        if (canvasRef0.current) {
            const t = tracks[0];
            draw(canvasRef0.current, t ? t.waveform : [], positions[0], t ? t.durationSec : 0);
        }
    }, [tracks[0], positions[0]]);

    useEffect(() => {
        if (canvasRef1.current) {
            const t = tracks[1];
            draw(canvasRef1.current, t ? t.waveform : [], positions[1], t ? t.durationSec : 0);
        }
    }, [tracks[1], positions[1]]);

    const formatTime = (sec: number) => {
        const m = Math.floor(sec / 60);
        const s = Math.floor(sec % 60);
        return `${m}:${s < 10 ? '0' : ''}${s}`;
    };

    return (
        <div className="container">
            <header className="app-header">
                <div className="brand">
                    <span className="logo-icon">⚡</span>
                    <h1>0XPLAY</h1>
                </div>
                <div className="automix-control">
                    <span className="control-label">HARMONIC AUTOMIX</span>
                    <button 
                        className={`toggle-btn ${autoMix ? 'active' : ''}`}
                        onClick={handleToggleAutoMix}
                    >
                        {autoMix ? 'ON' : 'OFF'}
                    </button>
                </div>
            </header>

            <main className="decks-layout">
                {[0, 1].map((slot) => {
                    const track = tracks[slot];
                    const canvasRef = slot === 0 ? canvasRef0 : canvasRef1;
                    return (
                        <section key={slot} className={`deck-card ${playing[slot] ? 'deck-active' : ''}`}>
                            <div className="deck-header">
                                <h2 className="deck-title">DECK {slot === 0 ? 'A' : 'B'}</h2>
                                <button className="load-btn" onClick={() => handleLoad(slot)}>
                                    <LoadIcon />
                                    <span>Load Track</span>
                                </button>
                            </div>

                            {track ? (
                                <div className="track-details">
                                    <h3 className="track-name">{track.title}</h3>
                                    <div className="stats-grid">
                                        <div className="stat-box">
                                            <span className="stat-label">BPM</span>
                                            <span className="stat-val">{track.bpm.toFixed(1)}</span>
                                        </div>
                                        <div className="stat-box">
                                            <span className="stat-label">KEY</span>
                                            <span className="stat-val signature-glowing">{track.keySignature}</span>
                                        </div>
                                        <div className="stat-box">
                                            <span className="stat-label">POSITION</span>
                                            <span className="stat-val">
                                                {formatTime(positions[slot])} / {formatTime(track.durationSec)}
                                            </span>
                                        </div>
                                    </div>

                                    <div className="visualizer-container">
                                        <canvas
                                            ref={canvasRef}
                                            width={600}
                                            height={120}
                                            className="waveform-canvas"
                                            onClick={(e) => handleCanvasClick(e, slot)}
                                        />
                                    </div>

                                    <div className="controls-grid">
                                        <div className="playback-controls">
                                            <button className="play-btn" onClick={() => handlePlayPause(slot)}>
                                                {playing[slot] ? <PauseIcon /> : <PlayIcon />}
                                            </button>
                                        </div>

                                        <div className="sliders-section">
                                            <div className="slider-group">
                                                <label>Volume</label>
                                                <input
                                                    type="range"
                                                    min="0"
                                                    max="1"
                                                    step="0.01"
                                                    value={volumes[slot]}
                                                    onChange={(e) => handleVolume(slot, parseFloat(e.target.value))}
                                                    className="slider"
                                                />
                                                <span className="slider-badge">{(volumes[slot] * 100).toFixed(0)}%</span>
                                            </div>

                                            <div className="slider-group">
                                                <label>Tempo</label>
                                                <input
                                                    type="range"
                                                    min="0.8"
                                                    max="1.2"
                                                    step="0.01"
                                                    value={tempos[slot]}
                                                    onChange={(e) => handleTempo(slot, parseFloat(e.target.value))}
                                                    className="slider"
                                                />
                                                <span 
                                                    className="slider-badge resetable"
                                                    onClick={() => handleTempo(slot, 1.0)}
                                                >
                                                    <ResetIcon />
                                                    <span>{tempos[slot].toFixed(2)}x</span>
                                                </span>
                                            </div>

                                            <div className="slider-group">
                                                <label>Pitch Shift</label>
                                                <input
                                                    type="range"
                                                    min="-2"
                                                    max="2"
                                                    step="1"
                                                    value={pitches[slot]}
                                                    onChange={(e) => handlePitch(slot, parseInt(e.target.value))}
                                                    className="slider"
                                                />
                                                <span 
                                                    className="slider-badge resetable"
                                                    onClick={() => handlePitch(slot, 0)}
                                                >
                                                    <ResetIcon />
                                                    <span>{pitches[slot] > 0 ? '+' : ''}{pitches[slot]} semitones</span>
                                                </span>
                                            </div>
                                        </div>
                                    </div>
                                </div>
                            ) : (
                                <div className="deck-empty">
                                    <div className="empty-state">
                                        <div className="empty-icon-container">
                                            <MusicIcon />
                                        </div>
                                        <p>No track loaded on this deck.</p>
                                        <button className="btn-primary" onClick={() => handleLoad(slot)}>
                                            <LoadIcon />
                                            <span>Select File</span>
                                        </button>
                                    </div>
                                </div>
                            )}
                        </section>
                    );
                })}
            </main>
        </div>
    );
}

export default App;
