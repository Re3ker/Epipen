# Epipen: latenzarmer Windows-Desktopfilter mit durchklickbarer Vollbildausgabe

## 1. Ziel, Ausgangslage und verbindliche Entscheidungen

### Ziel

Eine native Windows-Anwendung nimmt den Inhalt eines ausgewählten Monitors auf, verarbeitet die Bilder auf der GPU und zeigt ausschließlich die gefilterte Ansicht in einem randlosen Vollbildfenster auf demselben Monitor.

Im normalen Betrieb bleibt dieses Fenster durchklickbar. Maus und Tastatur bedienen weiterhin die Programme darunter.

Die erste Version enthält:

- Einstellbare Begrenzung des dargestellten Helligkeitsbereichs.
- Einen experimentellen, einstellbaren Filter gegen schnelle Farb- und Helligkeitsänderungen.
- Einen auswählbaren SDR-Monitor; weitere angeschlossene Monitore bleiben ungefiltert.
- Ein separates Einstellungsfenster, Tray-Menü und globale Hotkeys.
- Ruhige Ersatzdarstellung bei erkannten Fehlern.
- Messung von Bildrate, Verarbeitungszeit und interner Verzögerung.
- Reproduzierbaren Build, automatisierte Filtertests und dokumentierte manuelle Abnahme.

Die Anwendung wird als **experimentelle Reduktion visueller Reize** beschrieben. Sie diagnostiziert keine Epilepsie und garantiert keine Verhinderung von Anfällen. Die gewählten Filterparameter sind technische Ausgangswerte ohne medizinische Validierung.

### Ausgangslage

- Arbeitsordner: `C:\Users\Admin\Desktop\Epipen`.
- Der Ordner ist bei der Planung leer.
- Es wurden keine anwendbaren `AGENTS.md`-Dateien gefunden.
- Visual Studio Community 2026 mit C++-Werkzeugen ist vorhanden unter:
  `C:\Program Files\Microsoft Visual Studio\18\Community`.
- Windows SDK `10.0.26100.0`, C++/WinRT-Header und `fxc.exe` sind vorhanden.
- CMake und Ninja sind innerhalb der Visual-Studio-Installation vorhanden, aber nicht im aktuellen Shell-Suchpfad.
- GPU-Modell und tatsächliche Grafikkompatibilität wurden nicht festgestellt. Das ist zur Laufzeit zu prüfen.

### Festgelegte Grenzen

Zielplattform ist Windows 11 x64, lokal angemeldete interaktive Sitzung, Hardware-GPU mit Direct3D Feature Level 11.0 oder höher.

Nicht Bestandteil dieser Version:

- HDR-Filterung.
- Gleichzeitige Filterung mehrerer Monitore.
- Grafik-Hooks, DLL-Injection oder Grafiktreiber.
- Unterstützungsgarantie für exklusive Vollbildspiele, geschützte Videos, Remote Desktop oder Sicherheitsdesktop.
- Erkennung aller epileptogenen Muster oder eine vollständige WCAG-Blitzanalyse.
- Vorschau- oder Videopuffer zur Untersuchung zukünftiger Bilder.
- Autostart, Installer, Cloud-Dienste oder Telemetrie.

## 2. Architektur und technische Verträge

### Technologie

| Aufgabe | Festgelegte Umsetzung |
|---|---|
| Anwendung | C++20, MSVC, x64 |
| WinRT-Anbindung | C++/WinRT aus dem Windows SDK |
| Bildschirmaufnahme | Windows Graphics Capture |
| GPU-Verarbeitung | Direct3D 11, HLSL Shader Model 5.0 |
| Ausgabe | DirectComposition und DXGI-Kompositions-Swapchain |
| Fenster und Einstellungen | Win32, Common Controls |
| Build | CMake, Ninja, `fxc.exe` |
| Tests | Eigene kleine C++-Testprogramme über CTest |

Keine zusätzlichen Laufzeitframeworks und keine heruntergeladenen Drittanbieterbibliotheken verwenden.

DirectComposition wird bewusst für die kleine native Win32-Anwendung gewählt. Die benötigte Kombination aus Kompositionsausgabe, Layered Window, Click-through und Aufnahmeausschluss wird vor dem weiteren Ausbau praktisch geprüft.

### Datenfluss

```text
Ausgewählter Monitor
        │
        ▼
Windows Graphics Capture
        │ GPU-Textur, BGRA8
        ▼
Neuester verfügbarer Aufnahmeframe
        │
        ▼
Kopie in eigene GPU-Eingabetextur
        │
        ▼
HLSL: sRGB → linear → statischer Filter → Dynamikfilter
        │
        ▼
Lineare Verlaufstextur, Ping-Pong
        │
        ▼
HLSL: linear → sRGB
        │
        ▼
DXGI-Swapchain → DirectComposition → Vollbildfenster
```

Im Produktivpfad dürfen keine vollständigen Bildschirmbilder zur CPU zurückgelesen werden. Eine GPU-interne Kopie zur kontrollierten Lebensdauer des Aufnahmebildes ist erlaubt und vorgesehen. Keine Videoencoder oder Decoder einbauen.

### Module und Verantwortung

| Modul | Verantwortung |
|---|---|
| `Application` | Lebenszyklus, Zustandsautomat, Start, Stopp und Fehlerkoordination |
| `MonitorCatalog` | Monitore, physische Koordinaten, GPU-Zuordnung und SDR-Prüfung |
| `GraphicsDevice` | D3D11-Gerät, DXGI und GPU-Ressourcen |
| `CaptureSession` | WGC-Item, Frame-Pool, Ereignisse und neuester Frame |
| `FilterPipeline` | Eingabetextur, Verlaufstexturen, Shader und Parameter |
| `OverlayPresenter` | Ausgabe-HWND, DirectComposition, Swapchain und Präsentation |
| `SafetyCurtain` | GPU-unabhängig gezeichnete ruhige Ersatzfläche |
| `SettingsWindow` | Monitorwahl, Regler, Status und Diagnose |
| `SettingsStore` | Validierte lokale Konfiguration |
| `Diagnostics` | Zeitmessung, aggregierte Statistiken und Fehlerprotokoll |

Die GPU-Module besitzen keine Produktoberfläche. Ihre Fehler werden als strukturierte Ergebnisse an `Application` gemeldet.

### Interne Datentypen

Folgende Verträge implementieren; konkrete Hilfstypen dürfen ergänzt werden:

```cpp
enum class AppState {
    Stopped,
    Starting,
    Running,
    Quiet,
    Faulted,
    Stopping
};

struct FilterSettings {
    int blackFloorSrgb = 16;
    int whiteCeilingSrgb = 220;
    bool temporalEnabled = true;
    float maxChannelChangePerSecond = 1.0f;
};

struct MonitorSelection {
    std::wstring deviceName;
};

struct AppSettings {
    int version = 1;
    MonitorSelection monitor;
    FilterSettings filter;
};

struct FrameMetadata {
    uint64_t sequence;
    int64_t captureTime100ns;
    uint32_t width;
    uint32_t height;
};

struct RuntimeStats {
    double captureFps;
    double presentFps;
    double captureToSubmitMsP50;
    double captureToSubmitMsP95;
    double gpuFilterMsP50;
    double gpuFilterMsP95;
    uint64_t supersededCaptureFrames;
};
```

Fehler enthalten mindestens Kategorie, verständliche deutsche Meldung, technische Operation und gegebenenfalls HRESULT.

`FrameMetadata` beschreibt Aufnahmeframes. Zusätzlich intern eine eigene Sequenznummer für Ausgabeschritte führen: Der Dynamikfilter kann mehrere Ausgabeschritte auf Grundlage desselben Aufnahmeframes benötigen.

Es entsteht keine öffentliche Netzwerk- oder Plugin-API.

## 3. Aufnahme, Fenster, Threading und Latenz

### Monitorauswahl und Voraussetzungen

- Prozess vor der ersten Fenstererstellung auf Per-Monitor-DPI-Awareness V2 setzen.
- Monitore über Win32 und DXGI zuordnen.
- Den Adapter des ausgewählten Monitors für Aufnahme und Ausgabe verwenden.
- Physische Pixelkoordinaten aus `rcMonitor` verwenden, einschließlich negativer Desktopkoordinaten.
- Die vollständige Monitorfläche abdecken, einschließlich Taskleiste.
- Zunächst im Einstellungsfenster den primären Monitor auswählen; nicht automatisch starten.
- Vor Start WGC-Unterstützung, Hardwaregerät und Displaymodus prüfen.
- Aktiven HDR-/Advanced-Color-Modus über Windows-Displayinformationen prüfen. HDR aktiviert oder Status nicht zuverlässig bestimmbar: Start mit konkreter Fehlermeldung verweigern.
- Keine HDR-Einstellung und keine Displayhelligkeit automatisch ändern.
- Bei Neustart eine gespeicherte Monitorauswahl nur übernehmen, wenn sie noch existiert; andernfalls primären Monitor auswählen und eine erneute manuelle Aktivierung verlangen.

### Ausgabe- und Ersatzfenster

Zwei randlose Fenster vorbereiten:

1. **Ausgabefenster:** gefiltertes Livebild.
2. **Ersatzfenster:** gleichmäßige Fläche, über GDI mit `RGB(32,32,32)` gezeichnet.

Beide gehören zum eigenen Prozess und werden mit `WDA_EXCLUDEFROMCAPTURE` von der Bildschirmaufnahme ausgeschlossen. Rückgabewerte prüfen; bei Fehlschlag keinen Livebetrieb starten.

Das Ausgabefenster verwendet:

- `WS_POPUP`.
- `WS_EX_TOPMOST`.
- `WS_EX_TOOLWINDOW`.
- `WS_EX_NOACTIVATE`.
- `WS_EX_LAYERED | WS_EX_TRANSPARENT`.

Mit Layered-Window-Attributen volle Deckkraft konfigurieren. Die Eingabetransparenz darf keine optische Transparenz erzeugen. Die DirectComposition-Ausgabe verwendet überall Alpha `1`.

Nicht ausschließlich auf `HTTRANSPARENT` vertrauen, um Klicks zwischen unterschiedlichen Prozessen durchzureichen.

Das Ersatzfenster ist deckend und **nicht durchklickbar**. Bei eingefrorener Darstellung dürfen keine unsichtbaren Aktionen in den darunterliegenden Programmen ausgelöst werden. Globale Hotkeys und das Einstellungsfenster bleiben bedienbar.

Die Fenstersteuerung stellt sicher, dass entweder das Ersatzfenster oder die Liveausgabe die Fläche bedeckt. Beim Umschalten die neue Darstellung bereitstellen, bevor die bisherige Abdeckung verschwindet.

**Wichtig:** Ein Prozessabsturz, Treiberreset oder Wechsel zum Sicherheitsdesktop kann diese Abdeckung aufheben. Keine ausfallsichere Schutzwirkung behaupten.

### DirectComposition und Swapchain

Für die Kompositions-Swapchain festlegen:

- `DXGI_FORMAT_B8G8R8A8_UNORM`.
- `BufferCount = 2`.
- `DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL`.
- `DXGI_SCALING_STRETCH`.
- `DXGI_ALPHA_MODE_PREMULTIPLIED`.
- `SampleDesc.Count = 1`.
- `DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT`.
- `SetMaximumFrameLatency(1)`.
- `Present(1, 0)` als erster Betriebsmodus.

DirectComposition-Visual an die Swapchain und das Ausgabe-HWND binden. `Commit` bei Einrichtung und Änderungen am Visualbaum ausführen; keine unnötigen Commits pro Bild.

Kein exklusives Vollbild und kein `SetFullscreenState` verwenden.

### WGC-Aufnahme

- Monitor über `IGraphicsCaptureItemInterop::CreateForMonitor` auswählen.
- Frame-Pool mit `Direct3D11CaptureFramePool::CreateFreeThreaded` erstellen.
- Format `B8G8R8A8UIntNormalized`, zwei Aufnahmebuffer.
- Aufnahme des Mauszeigers deaktivieren. Den normalen Hardwarecursor sichtbar lassen, damit kein verzögerter zweiter Cursor entsteht.
- Systemseitigen Aufnahmerahmen in Version 1 nicht über zusätzliche Berechtigungen unterdrücken.
- `ContentSize`, geschlossene Capture-Items und Fehler behandeln.
- Aufnahmeframes nur bis zum Abschluss ihrer GPU-Kopie beziehungsweise Befehlsübergabe halten; anschließend freigeben.
- Beim Abholen alle bereits verfügbaren Frames durchgehen und nur den neuesten verarbeiten.
- Ressourcen und Ereignistoken mit RAII verwalten.

Keine Behauptung implementieren, dass ein schwarzer Frame automatisch auf DRM oder einen Defekt hinweist: Schwarze Bilder können legitimer Inhalt sein.

### Threads

**UI-Thread**

- Win32-Nachrichtenschleife.
- Fenster, Tray, Hotkeys und Einstellungen.
- Übergibt Steuerbefehle über eine threadsichere Queue.
- Führt keine Capture- oder GPU-Warteoperationen aus.

**Render-Thread**

- Als MTA initialisieren.
- Besitzt D3D11 Immediate Context, Filterressourcen und Präsentation.
- Erzeugt und verwaltet die Capture-Session.
- Verarbeitet UI-Befehle zwischen Ausgabeschritten.
- Wartet auf Stoppsignal, Befehle, Aufnahmeereignisse und Swapchain-Bereitschaft.

**WGC-Callback**

- Signalisiert lediglich die Verfügbarkeit neuer Bilder.
- Greift nicht auf den Immediate Context zu.
- Führt keine Shader, UI-Operationen oder blockierenden Arbeiten aus.

Damit ist der Immediate Context ausschließlich auf einem Thread in Verwendung.

### Latenzregeln

- Keine unbeschränkten Frame-Queues.
- Keine CPU-Bildkonvertierung.
- Keine zusätzliche Vorschaupufferung.
- Kein blockierendes GPU-Readback im Betrieb.
- Keine `Flush`-Aufrufe pro Frame.
- Kein Rendern schneller als Ausgabebereitschaft beziehungsweise Zielbildrate.
- Nicht verarbeitete Aufnahmeframes dürfen verworfen werden.
- Wenn der Dynamikfilter noch konvergiert, auf Basis des letzten Eingabebildes weiter rendern. Neue WGC-Frames sind dafür nicht erforderlich.
- Nach längeren Pausen darf der Filter keine große Zeitdifferenz in einen einzelnen großen Bildsprung umsetzen.

Ein statischer Desktop darf über längere Zeit keine neuen Capture-Frames liefern. Deshalb ist „seit 500 ms kein Aufnahmeframe“ allein **kein Aufnahmefehler**.

## 4. Filteralgorithmus, Bedienung und Fehlerverhalten

### Statischer Helligkeitsfilter

Der Regler begrenzt digitale Farbwerte. Er wird nicht als physische Helligkeit in Nits bezeichnet.

Verarbeitung ausschließlich in explizit linearisiertem sRGB:

```text
srgbToLinear(x):
    x <= 0.04045  → x / 12.92
    sonst        → ((x + 0.055) / 1.055)^2.4
```

Für jeden RGB-Kanal:

```text
inputLinear = srgbToLinear(inputSrgb)
F = srgbToLinear(blackFloorSrgb / 255)
C = srgbToLinear(whiteCeilingSrgb / 255)

target = F + (C - F) * inputLinear
```

Das komprimiert den gesamten Helligkeitsbereich: Schwarz wird auf die Untergrenze angehoben, Weiß auf die Obergrenze abgesenkt.

Defaults und Validierung:

| Einstellung | Default | Zulässiger Bereich |
|---|---:|---|
| Schwarzuntergrenze | 16 | Ganze Zahl, mindestens 0 |
| Weißobergrenze | 220 | Ganze Zahl, höchstens 255 |
| Abstand | — | `blackFloorSrgb < whiteCeilingSrgb` |

Ungültige Werte werden nicht an den Render-Thread übergeben.

Die Darstellung verändert sich dadurch bewusst auch bei normalen Bildern. Keine zusätzliche unklare „Kontrast“-Einstellung einführen; die beiden Endpunkte steuern diesen Effekt bereits.

### Dynamischer Filter

Der Filter begrenzt Farbänderungen pro Pixel ohne Zukunftspuffer.

Er ist **kein Blitzdetektor** und zählt keine Blitzfrequenzen. Er begrenzt jede schnelle Änderung, auch bewegte Kanten, Fensterwechsel und normale Animationen. Nachziehen und Farbveränderungen sind erwartete Nebenwirkungen.

Für jedes Pixel:

```text
P = linearer Ausgangswert des vorherigen Ausgabeschritts
T = Zielwert nach statischem Filter
D = T - P

dt = Zeit seit letztem erfolgreichen Ausgabeschritt
dt = clamp(dt, 0, 1/30 Sekunde)

M = max(abs(D.r), abs(D.g), abs(D.b))
B = maxChannelChangePerSecond * dt

a = 1, falls M praktisch 0 ist
sonst a = min(1, B / M)

Output = P + a * D
```

Der gemeinsame Faktor `a` erhält die Richtung der RGB-Änderung besser als drei voneinander unabhängige Clamps.

Festlegen:

- Default `maxChannelChangePerSecond = 1.0`.
- Einstellbereich `0.1 … 10.0`.
- Einheit in der Oberfläche: „Änderungsrate, linearer Farbwert pro Sekunde“.
- Zusatztext: „Kleinere Werte glätten stärker und erzeugen mehr Nachziehen.“
- Dynamikfilter standardmäßig eingeschaltet.
- Ausschalten des Dynamikfilters lässt den statischen Filter aktiv.

Die Einstellung `1.0` kann bei großen Bildwechseln deutlich sichtbare Übergangszeiten erzeugen. Sie ist ein überprüfbarer technischer Default, kein medizinischer Schwellenwert.

### Texturen und Farbkonvertierung

- Eingang: BGRA8 UNORM, als sRGB-codierte Werte interpretieren.
- Zwei Verlaufstexturen: `R16G16B16A16_FLOAT`.
- Verlauf mit Compute-Shader, Threadgruppen `8 × 8`.
- Jede Gruppe prüft Bildgrenzen.
- Vorherige Verlaufstextur als SRV, nächste als UAV.
- SRV/UAV nach Verwendung lösen, bevor Rollen getauscht werden.
- Auflösungsänderungen erzeugen neue Texturen; alte Inhalte nicht skalieren und als gültige Historie behandeln.
- Ausgabe mit Fullscreen-Triangle und Pixelshader.
- Im Pixelshader explizit linear nach sRGB konvertieren, danach in UNORM-Renderziel schreiben.
- Keine zusätzliche automatische sRGB-Konvertierung aktivieren.
- Alpha stets `1`.

Die Historie beschreibt zuletzt erfolgreich zur Präsentation übergebene Filterbilder. Sie ist kein Beweis dafür, welches Bild zu einem bestimmten Zeitpunkt physisch auf dem Display sichtbar war.

### Beginn und Parameteränderungen

- Bei Start die Historie mit der linearen Entsprechung der ruhigen Ersatzfläche initialisieren.
- Das erste Livebild durchläuft bereits den Dynamikfilter.
- Bei Änderungen an Filterparametern die Historie erhalten.
- Eine niedrigere Weißobergrenze sofort als harte Grenze auf den Verlauf anwenden.
- Eine höhere Schwarzuntergrenze ebenfalls sofort durchsetzen.
- Anschließend zum neuen Zielwert übergehen.
- Beim expliziten Ausschalten des Dynamikfilters darf unmittelbar der statisch gefilterte Zielwert erscheinen.
- Bei Größen- oder Gerätewechseln zunächst Ersatzfläche zeigen und Historie neu initialisieren.

### Oberfläche

Deutsches Einstellungsfenster mit:

- Monitorauswahl und Auflösung.
- Anzeige „SDR unterstützt“ oder konkretem Ablehnungsgrund.
- Start.
- „Ruhebild anzeigen“.
- „Fortsetzen“.
- „Filter beenden – Originaldesktop anzeigen“.
- Schwarzuntergrenze und Weißobergrenze als Slider plus Zahlenfeld.
- Dynamikfilter an/aus.
- Änderungsrate als Zahlenfeld plus Slider.
- Status und zuletzt aufgetretener Fehler.
- Diagnosebereich mit FPS, Messwerten und verworfenen Frames.
- Kurzer Hinweis auf experimentelle Reizreduktion und mögliche Nachzieheffekte.

Einstellungen können bedient werden, während die Liveausgabe läuft. Das Fenster darf dafür bewusst aktiviert und über der Ausgabe platziert werden. Es ist normale, ungefilterte Anwendungsoberfläche und wird ruhig gestaltet.

Das Einstellungsfenster ebenfalls von der Aufnahme ausschließen, damit es nicht als gefilterte Kopie wieder erscheint.

### Hotkeys und Tray

Fest vorsehen:

| Kombination | Aktion |
|---|---|
| `Ctrl+Alt+F9` | Ruhebild anzeigen; erneutes Drücken gibt nicht automatisch frei |
| `Ctrl+Alt+F10` | Einstellungsfenster öffnen |
| `Ctrl+Alt+F12` | Filter stoppen und Originaldesktop freigeben |

- `RegisterHotKey` mit Wiederholungsschutz verwenden.
- Bei Registrierungskonflikt Livebetrieb nicht starten und belegte Kombination nennen.
- Keine Low-Level-Tastatur- oder Maushooks.
- Tray-Menü: Einstellungen, Ruhebild, Fortsetzen, Filter stoppen, Anwendung beenden.
- Schließen des Einstellungsfensters beendet bei laufendem Filter nur das Fenster; die Anwendung bleibt im Tray.
- Beim ersten Schließen diesen Umstand einmal erläutern.
- Kein automatischer Start nach Programmstart oder Fehler.

### Zustandsautomat

| Zustand | Sichtbares Verhalten |
|---|---|
| `Stopped` | Kein Overlay; Originaldesktop sichtbar |
| `Starting` | Ruhige Ersatzfläche, Einrichtung läuft |
| `Running` | Gefilterte durchklickbare Ausgabe |
| `Quiet` | Ruhige Ersatzfläche, Mausklicks werden abgefangen |
| `Faulted` | Ruhige Ersatzfläche und Fehler im Einstellungsfenster |
| `Stopping` | Ressourcen geordnet schließen, danach Original freigeben |

Fehler während `Starting` oder `Running` führen zu `Faulted`, soweit der Prozess noch reagieren kann.

Bei einem Fehler:

1. Ersatzfläche sichtbar machen.
2. Neue Livepräsentationen stoppen.
3. Aufnahme und betroffene GPU-Ressourcen geordnet schließen.
4. Fehler und HRESULT protokollieren.
5. Einstellungsfenster mit konkretem Grund öffnen.
6. Erst nach „Fortsetzen“ einen vollständigen Neustart der Pipeline versuchen.

Keine automatische Wiederholungsschleife und kein stiller Wechsel zu einem anderen Aufnahmeverfahren.

Bei `Quiet` Aufnahme stoppen, um keine unnötige GPU-Last zu erzeugen. „Fortsetzen“ initialisiert die Pipeline neu.

Bei Sitzungswechsel, Sperren, Displaytrennung, Auflösungsänderung oder aktivierter HDR-Ausgabe die laufende Sitzung verlassen. Nach Rückkehr beziehungsweise neuer Monitorkonfiguration eine ausdrückliche Fortsetzung verlangen.

### Speicherung und Logging

Konfiguration lokal als versionierte INI-Datei unter:

```text
%LOCALAPPDATA%\Epipen\settings.ini
```

Keine JSON-Bibliothek nur für diese wenigen Einstellungen hinzufügen.

- Beim Laden vollständig validieren.
- Unbekannte Schlüssel ignorieren.
- Beschädigte oder ungültige Konfiguration führt zu Defaults und einem sichtbaren Hinweis.
- Änderungen über temporäre Datei und Ersetzen speichern.
- Monitor über Gerätenamen speichern; keine dauerhafte Existenz voraussetzen.

Protokoll unter `%LOCALAPPDATA%\Epipen\logs`, maximal drei Dateien zu jeweils 1 MiB.

Protokollieren: Version, Windows-Build, Adapter, Monitorauflösung, Zustandswechsel, HRESULTs und aggregierte Leistungswerte. Keine Screenshots, Pixelinhalte, Fenstertitel oder Tastatureingaben speichern.

## 5. Umsetzung in aufeinander aufbauenden Schritten

### Schritt 1: Projekt und Build

Anlegen:

```text
CMakeLists.txt
CMakePresets.json
README.md
docs/implementation-plan.md
docs/acceptance.md
scripts/
src/
shaders/
tests/
```

Den vollständigen eigenständigen Plan bei der Umsetzung in `docs/implementation-plan.md` ablegen.

Buildkonfiguration:

- C++20, Unicode, `/W4`, `/permissive-`, `/EHsc`, `/utf-8`.
- x64.
- Debug und Release als CMake-Presets.
- SDK-Version `10.0.26100.0`.
- Shader beim Build mit `fxc.exe` kompilieren und als generierte Header in das Programm einbetten.
- Keine Shaderdateien zur Laufzeit suchen.
- Benötigte Systembibliotheken ausdrücklich linken, insbesondere D3D11, DXGI, DirectComposition, Windows Runtime, DWM, User32, GDI32, Shell32 und Common Controls.
- Keine fehlenden Buildwerkzeuge still installieren.

Ein PowerShell-Buildskript verwendet `vswhere.exe`, lädt `Launch-VsDevShell.ps1` und findet das mitgelieferte CMake/Ninja.

Die dokumentierte Bedienung muss anschließend lauten:

```powershell
.\scripts\build.ps1 -Configuration Debug
.\scripts\build.ps1 -Configuration Release
.\scripts\test.ps1
```

Die Skripte müssen aus einer gewöhnlichen PowerShell funktionieren.

**Abnahme:** Leeres Programm mit Einstellungsfenster baut und startet.

### Schritt 2: Machbarkeitsprüfung vor Filterentwicklung

Zuerst nur folgende Kombination implementieren:

- Aufnahme desselben Monitors.
- Eigene deckende DirectComposition-Ausgabe.
- Aufnahmeausschluss des Ausgabefensters.
- Click-through.
- Hotkey zum sicheren Beenden.
- Ruhige Ersatzfläche.

Eine Hilfsanwendung zeigt im Hintergrund eine langsam wandernde Form und einen Zähler. Keine blinkenden Testbilder sichtbar darstellen.

Prüfen:

- Der Zähler bewegt sich unter dem Overlay weiter.
- Die Aufnahme enthält das Hintergrundbild, keine Eigenkopie des Overlays.
- Die Ausgabe bleibt deckend.
- Klicken, Scrollen und Ziehen erreichen andere Prozesse.
- Die Tastatur bleibt bei der bedienten Anwendung.
- Der Cursor erscheint genau einmal.
- Die Darstellung wächst nicht zu einer Rückkopplungsschleife an.

**Verbindliches Gate:** Wenn diese Kombination auf dem Testsystem nicht funktioniert, keine fertige Desktopfilterfunktion behaupten und nicht zuerst die restliche Oberfläche ausbauen. Den Fehler reproduzierbar dokumentieren. Kein stiller Ersatz durch Grafik-Hooks, virtuelle Displays oder CPU-Kopien.

### Schritt 3: Statischer Filter

- Farbkonvertierungen und Endpunktabbildung implementieren.
- CPU-Referenzfunktionen für Tests schreiben.
- Shaderausgaben gegen die Referenz prüfen.
- Regler live anbinden.
- Start- und Stoppübergänge ohne ungefilterte Zwischenbilder innerhalb der kontrollierten Ausgabe prüfen.

**Abnahme:** Bekannte Eingangswerte liefern die erwarteten digitalen Ausgabewerte; Weiß überschreitet die eingestellte Grenze nicht.

### Schritt 4: Dynamischer Filter

- Float-Verlaufstexturen und Ping-Pong-Verarbeitung ergänzen.
- Algorithmus exakt nach Abschnitt 4 implementieren.
- Zeitdifferenzen begrenzen.
- Auf unveränderten Eingaben bis zum Ziel weiterarbeiten.
- Konvergenz GPU-seitig feststellen: eine kleine Reduktion des maximalen Restfehlers vorsehen; Ergebnis nur asynchron und verzögert für die Entscheidung „weitere Ausgaben erforderlich“ abholen.
- Falls das Ergebnis noch nicht bereitsteht, weiterarbeiten statt synchron zu warten.
- Keine automatische Abschaltung anhand von CPU-Pixelprüfungen.

**Abnahme:** Änderungsgrenzen gelten für jeden simulierten Ausgabeschritt; statische Ziele werden erreicht; es gibt keinen dauerhaften Nachlauf wegen fehlender Capture-Ereignisse.

### Schritt 5: Lebenszyklus und Oberfläche

- Zustandsautomat, Tray, Einstellungen und Persistenz vervollständigen.
- Ruhemodus und Fehlerfenster integrieren.
- Geräteverlust und Monitorkonfigurationsänderungen behandeln.
- Wiederholtes Starten und Stoppen auf freigegebene COM-Objekte, Ereignisse und Handles prüfen.

**Abnahme:** Alle Übergänge sind aus Oberfläche und Hotkeys erreichbar; nach Fehlern erfolgt kein selbstständiger Wiedereinstieg.

### Schritt 6: Diagnose und Performance

- Aufnahmezeitstempel mit QPC-kompatibler Zeitbasis auswerten.
- Zeit von Capture-Zeitstempel bis Übergabe an `Present` erfassen.
- GPU-Zeit mit Timestamp- und Disjoint-Queries messen.
- Query-Ergebnisse über einen Ring mehrerer Ausgabeschritte später und nicht blockierend lesen.
- Statistiken höchstens einmal pro Sekunde in der UI aktualisieren.
- P50/P95 über ein rollendes Fenster von zehn Sekunden berechnen.

Die Messung „Capture bis Present-Aufruf“ ausdrücklich als interne Zeit bezeichnen. Sie enthält weder vollständigen Scanout noch Displayreaktion und ist keine vollständige sichtbare Ende-zu-Ende-Latenz.

### Schritt 7: Dokumentation und portable Ausgabe

Liefern:

- Release-EXE in einem eindeutig benannten Ausgabeordner.
- Erforderliche Laufzeitvoraussetzungen beziehungsweise mitgelieferte zulässige Runtime-Dateien dokumentieren.
- Build- und Startanleitung.
- Filterformeln, Defaults und bekannte Bildartefakte.
- Unterstützte und nicht geprüfte Grafikpfade.
- Liste tatsächlich durchgeführter Tests und verwendeter Hardware.
- Verbleibende Fehler und Einschränkungen.
- Konkrete Messwerte statt pauschaler Aussagen wie „latenzfrei“ oder „schützt vor Anfällen“.

## 6. Tests und messbare Abnahme

### Automatisierte Mathematiktests

Über CTest ausführen:

- sRGB-/Linear-Konvertierung an Schwarz, Weiß, Übergangspunkten und Zwischenwerten.
- Endpunktabbildung: Schwarz auf Untergrenze, Weiß auf Obergrenze.
- Monotonie des statischen Filters.
- Validierung ungültiger Einstellungen.
- Dynamischer Filter bei konstantem Bild.
- Schwarz-Weiß-Sprung in beide Richtungen.
- Reine Rot-Grün- und Rot-Schwarz-Änderungen.
- Wechselnde Bilder mit unterschiedlichen Schrittzeiten.
- Große Zeitlücke: kein unbegrenzter Sprung.
- Dynamikfilter deaktiviert.
- Grenzänderungen während laufender Historie.
- Initialisierung, Resize und Neustart der Historie.
- Jeder Schritt erfüllt `max(abs(Output − Previous)) <= rate × dt`, außer ausdrücklich sofort durchgesetzten statischen Grenzen.

### GPU-Tests ohne sichtbare Blitzdarstellung

Kleine synthetische Texturen außerhalb eines sichtbaren Fensters durch dieselben Shader verarbeiten und nur im Testprogramm zurücklesen.

Vergleichen:

- CPU-Referenz gegen GPU-Ergebnis.
- Float-Verlauf mit numerischer Toleranz `2e-3`.
- Finale 8-Bit-Ausgabe mit maximal zwei Codewerten Abweichung.
- Ungewöhnliche Bildgrößen wie `17 × 13`.
- BGRA-Kanalreihenfolge.
- Keine doppelte sRGB-Kodierung.
- Alle Werte endlich, im zulässigen Bereich, Alpha `1`.
- Wiederholte Resize-/Neustartzyklen ohne Debug-Layer-Fehler.

D3D-WARP darf für solche kleinen Offscreen-Tests verwendet werden. Es ist kein Produktiv-Fallback.

### Manuelle Funktionsabnahme

Auf mindestens einem dokumentierten Windows-11-System:

- Ein Monitor und zusätzliche angeschlossene, ungefilterte Monitore.
- Zielmonitor links vom primären Monitor mit negativen Koordinaten.
- 100 %, 150 % und 200 % Skalierung, soweit verfügbar.
- Linksklick, Rechtsklick, Doppelklick, Scrollen, Drag-and-drop.
- Texteingabe, Tastenkombinationen und Alt-Tab.
- Browser, Texteditor und normale Desktopanwendungen.
- Ein ungeschütztes Fenstervideo.
- Verschieben von Fenstern zwischen gefiltertem und ungefiltertem Monitor.
- Ruhemodus verhindert Klicks in die verborgene Oberfläche.
- Hotkeykonflikte.
- Bildschirm sperren und entsperren.
- Auflösung ändern, Monitor trennen und wieder verbinden.
- HDR vor Start sowie Aktivierung während des Betriebs.
- Simulierte Capture- und Grafikfehler.
- Anwendung normal beenden und separat Prozessabsturz beobachten.

Nicht verfügbare Hardwarefälle als „nicht geprüft“ dokumentieren.

### Performance-Abnahme

Referenz zunächst ein SDR-Monitor mit 1920 × 1080 bei 60 Hz, native Auflösung, beide Filter aktiv, Release-Build.

Ziele, die auf der dokumentierten Referenzhardware geprüft werden:

- Kein wachsender Frame-Rückstand über zehn Minuten.
- Bei kontinuierlich aktualisierter Quelle mindestens 95 % der Zielbildrate.
- GPU-Filter- und Ausgabearbeit P95 höchstens 4 ms.
- Interne Zeit von Capture-Zeitstempel bis Present-Aufruf P95 höchstens 25 ms.
- Keine regelmäßigen CPU-Pixelkopien.
- Keine fortlaufende Zunahme von Handles oder GPU-Ressourcen nach 30 Start-/Stoppzyklen.

Diese Werte sind Entwicklungsziele, keine Hardwaregarantie. Bei Nichterreichen Messwerte und Engpass offen dokumentieren; keine Tests abschwächen oder Messgrößen umbenennen.

Die sichtbare Zusatzlatenz separat mit einer externen Hochgeschwindigkeitsaufnahme und einer ruhigen bewegten Markierung untersuchen. Ungefilterten und gefilterten Betrieb unter gleichen Bedingungen vergleichen. Vorhandene Ausstattung und Messunsicherheit angeben.

### Fertigstellungskriterium

Die Version ist implementiert, wenn:

1. Die Capture-/Overlay-/Click-through-Machbarkeitsprüfung bestanden ist.
2. Beide Filter nach den festgelegten Formeln arbeiten.
3. Automatisierte CPU- und GPU-Tests bestanden sind.
4. Die Kernbedienung und Fehlerübergänge manuell bestanden sind.
5. Performancewerte erhoben und mit den Zielen verglichen wurden.
6. Ein anderer Entwickler ausschließlich anhand von Repository und Dokumentation bauen, starten und prüfen kann.

Eine bestandene technische Abnahme ist keine medizinische Wirksamkeitsprüfung.

## 7. Technische Referenzen und Grenzen der Aussagen

- [Microsoft: Windows Graphics Capture](https://learn.microsoft.com/en-us/windows/apps/develop/media-authoring-processing/screen-capture) – Bildschirm- und Fensteraufnahme.
- [Microsoft: CreateFreeThreaded](https://learn.microsoft.com/en-us/uwp/api/windows.graphics.capture.direct3d11captureframepool.createfreethreaded) – Aufnahmeereignisse unabhängig von einer UI-DispatcherQueue.
- [Microsoft: SetWindowDisplayAffinity](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setwindowdisplayaffinity) – Ausschluss eigener Fenster aus unterstützten Aufnahmewegen; praktisch mit der gewählten Pipeline prüfen.
- [Microsoft: Window Features](https://learn.microsoft.com/en-us/windows/win32/winmsg/window-features) – Click-through bei Layered Windows mit `WS_EX_TRANSPARENT`.
- [Microsoft: CreateSwapChainForComposition](https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_2/nf-dxgi1_2-idxgifactory2-createswapchainforcomposition) – Anforderungen an Kompositions-Swapchains.
- [Microsoft: DirectComposition-Grundlagen](https://learn.microsoft.com/en-us/windows/win32/directcomp/basic-concepts) – Anbindung von Visuals und Layered Windows.
- [Microsoft: DXGI-Latenz reduzieren](https://learn.microsoft.com/en-us/windows/uwp/gaming/reduce-latency-with-dxgi-1-3-swap-chains) – Warteobjekt und begrenzte Ausgabewarteschlange.
- [Epilepsy Action: Photosensitive epilepsy](https://www.epilepsy.org.uk/info/seizure-triggers/photosensitive-epilepsy) – visuelle Auslöser umfassen mehr als maximale Helligkeit.
- [W3C: Three Flashes or Below Threshold](https://www.w3.org/WAI/WCAG21/Understanding/three-flashes-or-below-threshold) – fachlicher Hintergrund; die hier beschriebene Änderungsbegrenzung implementiert keine vollständige Prüfung dieses Kriteriums.
