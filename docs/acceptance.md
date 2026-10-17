# Abnahmeprotokoll

## GPU-Filter und Renderpfad

Status: **automatisierte GPU-Prüfung bestanden; manuelle Prüfung nach dem Filterfix offen.** Vor dem Fix wurde die Desktopaufnahme sichtbar ausgegeben, aber ungefiltert; die Regler hatten keine Wirkung. Die neue GPU-Pipeline wurde mit dem Windows-WARP-Renderer pixelweise geprüft.

| Prüfung | Ergebnis |
|---|---|
| Datum der Codeprüfung | 2026-10-17 |
| Debug-Build | bestanden |
| Release-Build | bestanden |
| CPU-Mathematiktests (Debug) | bestanden |
| GPU-WARP-Pixeltests (Debug) | bestanden: sRGB-Verlauf, BGRA-Kanäle, ungerade Bildgröße |
| Live-Parameteränderung im GPU-Test | bestanden |
| Zeitliche Kanaländerung im GPU-Test | bestanden |
| CPU- und GPU-Tests (Release) | bestanden |
| Ziel-GPU und Treiber | noch nicht erfasst |
| Windows-Version/Build und Monitor | noch nicht erfasst |
| Gefiltertes Livebild auf Ziel-GPU | noch nicht manuell geprüft |
| Reglerwirkung auf Ziel-GPU | noch nicht manuell geprüft |
| Klickdurchleitung und Cursor | noch nicht manuell geprüft |
| Ausschluss der Epipen-Fenster aus der Aufnahme | noch nicht manuell geprüft |
| Ruhebild, Hotkeys sowie Start/Stop | noch nicht manuell geprüft |
| Bildrate und Filterlatenz auf Ziel-GPU | noch nicht gemessen |

## Manuelle Abnahme nach dem Fix

1. `epipen_background.exe` starten und bewegte Scheibe sowie Zähler sichtbar lassen.
2. `epipen.exe` starten, einen SDR-Monitor auswählen und **Filter starten** wählen.
3. Prüfen, dass sich das Bild sichtbar verändert. Schwarz- und Weißgrenze verstellen und die Wirkung prüfen. Mit aktivierter zeitlicher Begrenzung die Änderungsrate variieren.
4. Im Hintergrundfenster klicken, scrollen, ziehen und tippen. Prüfen, dass die Eingaben durchgehen, der Cursor einmal erscheint und Epipen nicht rekursiv aufgenommen wird.
5. Mit `Ctrl+Alt+F10` die Einstellungen öffnen. `Ctrl+Alt+F9` muss Eingaben mit dem Ruhebild abfangen; `Ctrl+Alt+F12` muss den Desktop freigeben.

GPU, Treiber, Windows-Build, Monitor, Modus und Ergebnis je Prüfschritt nach dem Lauf ergänzen. Ein bestandener WARP-Test belegt die Shaderausgabe, aber nicht die DWM-Komposition oder den Durchklickbetrieb auf einer konkreten Hardwarekonfiguration.
