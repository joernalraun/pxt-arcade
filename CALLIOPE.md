# MakeCode Arcade – Calliope-Edition

Basis: `microsoft/pxt-arcade` **v4.2.1** (pxt-core 13.2.1), Branch `calliope-n3`.

## Änderungen gegenüber Upstream

| Datei | Änderung |
|-------|----------|
| `editor/extension.ts` | Hardware fest auf `n3` („Calliope mini") gesetzt → kein „Hardware auswählen"-Dialog, Download immer als n3-`.hex`. WebUSB-Filter auf DAPLink (VID `0x0D28`/PID `0x0204`) + DAPLink-Packet-IO-Wrapper. |
| `editor/flash.ts`, `editor/dapjs.d.ts`, `editor/prepend/dapjs.js` | DAPLink-WebUSB-Flasher aus `pxt-microbit` (MIT; in v8.1.15 und v9.1.1 identisch), übernommen aus `pxt-arcade-master` inkl. Namespace-Wrap und `binary.hex`-Fallback. |
| `pxtarget.json` | Variante `nrf52833`: `webUSB: true` + `hidSelectors`. `appTheme`: `preferWebUSBDownload`, `boardName`/`boardNickname` = „Calliope mini", `driveDisplayName` = „MINI", `downloadDialogTheme` wie pxt-calliope. |
| `docs/static/download/*` | Bilder für den Verbindungsdialog aus `microsoft/pxt-calliope` (MIT). |
| `theme/style.less` | Blendet den Menüeintrag „Hardware auswählen" im Download-Menü aus. |
| `libs/jacdac/` | `jacdac/pxt-jacdac` **v1.9.42** (MIT) als interne Bibliothek für den Mehrspielermodus – kein GitHub-Download nötig, in der Erweiterungsliste ausgeblendet (`hidden`). Abhängigkeiten auf `file:../core`/`file:../settings` umgestellt; `initSim()` öffnet den Jacdac-Simulator-Zusatz nicht; die beiden „Roles“-Blöcke sind per `blockHidden` ausgeblendet (keine leere Kategorie „Modules“). Treiber-Korrekturen siehe unten. |
| `libs/jacdac-multiplayer/` | Mehrspielermodus (Host + 1 Gast per Jacdac-Kabel), als empfohlene Erweiterung gelistet. Verbindet nur Geräte mit demselben Spiel (Programmkennung `control.programHash()` in der Host-Meldung), sonst Hinweis „Anderes Spiel erkannt“. JavaScript-API für beide Bildschirme: `sendGameState(buf)` (Host), `onGameState(handler)` und `setGuestRenderer(fn)` (Gast), `isGuest()`. |
| `docs/reference/jacdac-multiplayer.md` | Referenzseite (deutsch) hinter „Weitere Informationen“: Ablauf, Spieler-2-Blöcke, zwei Beispiele, Tipps. Kartenbild `docs/static/libs/jacdac-multiplayer.png`. |
| `pxtarget.json` / `targetconfig.json` | Beide Bibliotheken in `bundleddirs`; `jacdac-multiplayer` unter „Empfohlen"/„Hardware". Die normale Jacdac-Erweiterung (`jacdac/pxt-jacdac` von GitHub, mit Simulator-Zusatz) bleibt unverändert verfügbar. |

## Jacdac-Treiber: Korrekturen für Calliope mini + GameKit

Alle Änderungen sind im Code mit `Calliope edition` markiert.

| Problem | Korrektur |
|---------|-----------|
| **~5-s-Aussetzer** (Ursache): CODALs GPIOTE-Handler löscht alle LATCH-Bits nach einer Momentaufnahme. Eine Flanke am Display-Flow-Pin P0_10 während des Handlers geht verloren, ihre Sense-Bedingung bleibt erfüllt, das gemeinsame DETECT-Signal fällt nie ab – kein Pin bekommt mehr Interrupts, Jacdac ist taub, bis der Display-Controller nach ~5 s wechselt. | `hw.cpp`: beim Start `DETECTMODE = LDETECT` (Latched Detect) für P0/P1 – der nRF52 löst dann selbst ein neues PORT-Ereignis aus, wenn nach dem Löschen noch LATCH-Bits gesetzt sind. In CODAL ist diese Zeile seit 2020 auskommentiert (vermutlich wegen möglicher Interrupt-Stürme bei Pins mit Sense ohne Flankenverarbeitung, z. B. Wake-Pins; Arcade nutzt so etwas nicht). Sicherheitsnetz: `__physGpioWatchdog()` (`app.cpp`) erkennt einen Pin, der zwei Prüfungen in Folge seine Sense-Bedingung erfüllt, und löst das PORT-Ereignis erneut aus bzw. dreht die Sense-Richtung um. |
| Gerät sendet nach Leitungsfehlern nie wieder: ein veraltetes ENDRX wird während des Sendens als „Empfang fertig“ gemeldet, `abortDMA()` schaltet den TX-fertig-Interrupt ab. | `hw.cpp` ignoriert ENDRX während des Sendens; `jdlow.c` setzt zusätzlich einen Sendevorgang zurück, der nach 5 ms nicht fertig ist. |
| Empfang bleibt stehen, wenn das einzige `DATA_READY`-Ereignis verloren geht. | `app.cpp` löst es erneut aus (Puffer voll oder Paket > 100 ms ungelesen); `routing.ts` liest zusätzlich alle 200 ms liegengebliebene Pakete. |
| Statusmaschine hängt nach verlorenem Timer- oder UART-Interrupt. | `jd_watchdog()` (`jdlow.c`): zwei Prüfungen in Folge beschäftigt ohne Fortschritt → Treiber zurücksetzen. |
| Jacdac-Timer kann in CODALs fester 10er-Timerliste verloren gehen (neuer Timer wurde vor dem Löschen des alten gestellt, Ergebnis nicht geprüft). | `tim_set_timer()` (`hw.cpp`) löscht zuerst, prüft das Ergebnis; ein verlorener Timer wird vom Wächter neu gestellt. |
| Volle Sende-Warteschlange: neues Paket verworfen, veraltete werden zuerst gesendet (~1 s Verzögerung); verpasstes Anstoßen lässt Pakete liegen. | `app.cpp`: ältestes Paket verwerfen; Sender bei voller Warteschlange und vom Wächter anstoßen. |
| Pakete gehen bei langsamen Arcade-Frames verloren. | Empfangspuffer `MAX_RX` 10 → 24 (Speicher nur pro wartendem Paket). |

Der Mehrspielermodus selbst (`libs/jacdac-multiplayer`) sendet den Tastenzustand alle 100 ms und bei jeder Änderung, mit Druckzählern pro Taste; der Host lässt die Tasten nach 0,3 s ohne Daten los und trennt erst nach 6 s.

Beispielspiel **Tennis** (GameBoy-Stil, allein gegen den Calliope mini oder zu zweit; beide Bildschirme zeigen das Spiel, der Gast gespiegelt): `projects/tennis`. Der Host schickt ~15×/s einen 30-Byte-Spielzustand, der Gast zeichnet daraus und rechnet den Ball zwischen zwei Paketen weiter.

Beispielspiel **Mini Racer** (Autorennen von oben im Stil von „Micro Machines“, Kopf-an-Kopf: die Kamera folgt dem Führenden, wer aus dem Bild fällt oder ins Aus fährt, verliert die Runde; nach dem Sieg Siegerehrung mit Feuerwerk, dann Titelbild; allein gegen den Calliope mini oder zu zweit, beide Bildschirme zeigen dieselbe Ansicht): `projects/micromachines`. Eigene Farbpalette mit Grautönen über `image.setPalette()`.

Testspiel (zwei Figuren): `projects/mptest` (nicht im Repo, `projects/` ist ignoriert), bauen mit `node ../../node_modules/pxt-core/built/pxt.js build --hw n3`.

Jacdac läuft beim Calliope mini V3 als Single-Wire-Bus an **P12 (P0_12)**, beide Jacdac-Buchsen hängen am selben Bus. In Projekten: `"jacdac": "*"`.

Ablauf beim Download (wie MakeCode für Calliope mini): „Verbinde deinen Calliope mini" → „Koppeln" → Browser-Geräteauswahl → Flashen per WebUSB (Teil-Flash bei Folge-Downloads). Danach sind „Gerät verbinden"/„Trennen" und „Als Datei herunterladen" im `…`-Menü.

## Lokal starten

```bash
npm install
PXT_NODOCKER=1 node node_modules/pxt-core/built/pxt.js serve --no-browser
```

WebUSB benötigt Chrome/Edge und `localhost` bzw. HTTPS. Nach Änderungen Service Worker entfernen bzw. Inkognito nutzen (MakeCode cacht das Editor-Bundle).

Hinweis: `pxt-arcade-sim` (optionale Abhängigkeit) ist ein privates Microsoft-Repo und wird lokal nicht installiert; der Simulator-Bildschirm bleibt beim lokalen Serve deshalb schwarz.
