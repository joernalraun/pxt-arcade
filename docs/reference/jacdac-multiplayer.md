# Jacdac-Mehrspielermodus

Spielt zu zweit: Verbinde zwei Calliope mini mit GameKit über ein **Jacdac-Kabel**. Ein Calliope mini führt das Spiel aus, der zweite wird zum Controller für **Spieler 2**.

![Jacdac-Kabel](/static/libs/jacdac-multiplayer.png)

## So funktioniert es

1. Spiele **dasselbe Programm** auf beide Calliope mini. Die Erweiterung prüft das: Läuft auf dem anderen Calliope mini ein anderes Spiel, erscheint kurz der Hinweis **„Anderes Spiel erkannt“** statt der Frage zum Mitspielen.
2. Verbinde die beiden Geräte über ihre **Jacdac-Buchsen** mit einem Jacdac-Kabel.
3. Auf beiden Displays erscheint die Frage **„Calliope mini erkannt! Mitspielen?“**.
4. Drücke auf **einem** der beiden Geräte **A = Ja**. Dieses Gerät wird zum Controller und zeigt **„Verbunden – Du bist Spieler 2“**.
5. Auf dem anderen Gerät läuft das Spiel weiter. Dort steuerst du **Spieler 1**, der Controller steuert **Spieler 2**.

Mit **B = Nein** spielt jedes Gerät sein eigenes Spiel weiter.

## ~ hint

**Nur ein Bildschirm zeigt das Spiel.** Das Spiel läuft auf dem Gerät, auf dem niemand „Ja“ gedrückt hat. Der Controller zeigt nur, ob er verbunden ist und welche Tasten gerade gedrückt sind.

## ~

## Spieler 2 im Programm

Die Erweiterung hat keine eigenen Blöcke. Du programmierst Spieler 2 mit den normalen **Controller**-Blöcken für **Spieler 2**: Alles, was am Controller gedrückt wird, kommt im Spiel als Taste von Spieler 2 an.

```cards
controller.player2.moveSprite(null)
controller.player2.onButtonEvent(ControllerButton.A, ControllerButtonEvent.Pressed, function () {})
controller.player2.isPressed(ControllerButton.A)
controller.player2.onEvent(ControllerEvent.Connected, function () {})
```

Übertragen werden die Richtungstasten sowie **A** und **B**. Die **Menü**-Taste bleibt auf dem Controller.

## Beispiel: Zwei Figuren #example

Spieler 1 steuert die rote Figur, Spieler 2 am zweiten Calliope mini die grüne. Mit **A** sagt die jeweilige Figur „Hallo!“. Sobald der Controller verbunden ist, erscheint eine Meldung.

```blocks
let rot = sprites.create(img`
    . . 2 2 2 2 . .
    . 2 2 2 2 2 2 .
    2 2 1 2 2 1 2 2
    2 2 2 2 2 2 2 2
    2 2 2 2 2 2 2 2
    2 2 1 1 1 1 2 2
    . 2 2 2 2 2 2 .
    . . 2 2 2 2 . .
`, SpriteKind.Player)
let gruen = sprites.create(img`
    . . 7 7 7 7 . .
    . 7 7 7 7 7 7 .
    7 7 1 7 7 1 7 7
    7 7 7 7 7 7 7 7
    7 7 7 7 7 7 7 7
    7 7 1 1 1 1 7 7
    . 7 7 7 7 7 7 .
    . . 7 7 7 7 . .
`, SpriteKind.Player)
rot.setPosition(50, 60)
gruen.setPosition(110, 60)
rot.setStayInScreen(true)
gruen.setStayInScreen(true)
controller.moveSprite(rot)
controller.player2.moveSprite(gruen)
controller.A.onEvent(ControllerButtonEvent.Pressed, function () {
    rot.sayText("Hallo!", 500)
})
controller.player2.onButtonEvent(ControllerButton.A, ControllerButtonEvent.Pressed, function () {
    gruen.sayText("Hallo!", 500)
})
controller.player2.onEvent(ControllerEvent.Connected, function () {
    game.splash("Spieler 2 ist da!")
})
```

## Beispiel: Wenn Spieler 2 das Kabel zieht

Kommen keine Daten mehr vom Controller, lässt das Spiel nach kurzer Zeit alle Tasten von Spieler 2 los, damit seine Figur stehen bleibt. Erst nach etwa 6 Sekunden ohne Verbindung meldet das Spiel, dass Spieler 2 nicht mehr verbunden ist – kurze Aussetzer auf dem Kabel führen so nicht zu einer Trennung.

```blocks
controller.player2.onEvent(ControllerEvent.Disconnected, function () {
    game.splash("Spieler 2 ist weg", "Kabel prüfen!")
})
```

Auf dem Controller erscheint **„Verbindung getrennt“**. Mit **B** startet er neu und kehrt zu seinem eigenen Spiel zurück.

## ~ hint

**Tipps**

* **Strom:** Jeder Calliope mini braucht seine eigene Stromversorgung (USB oder Batterie). Über das Jacdac-Kabel wird kein zweites GameKit versorgt.
* **Zwei Spieler:** Der Mehrspielermodus verbindet genau **zwei** Calliope mini.
* **Neu verbinden:** Wurde „Nein“ gewählt, starte beide Calliope mini neu, damit die Frage wieder erscheint.
* **Kabel:** Beide Jacdac-Buchsen des Calliope mini funktionieren gleich – du kannst jede verwenden.
* **Nicht mischen:** Verwende in einem Projekt nicht gleichzeitig diese Erweiterung und die normale **Jacdac**-Erweiterung.

## ~

## Für Fortgeschrittene: das Spiel auf beiden Bildschirmen

Normalerweise zeigt nur der Host das Spiel. In **JavaScript** kannst du den zweiten Calliope mini auch das Spielfeld zeigen lassen: Der Host schickt regelmäßig einen kleinen **Spielzustand** (z. B. Positionen und Punkte, bis etwa 200 Byte), und der Gast zeichnet daraus seine eigene Ansicht.

```typescript-ignore
// Host: etwa 10-20 mal pro Sekunde den Zustand schicken
const state = Buffer.create(4)
state[0] = info.score()
jacdac.multiplayer.sendGameState(state)

// Gast: Zustand empfangen ...
let punkte = 0
jacdac.multiplayer.onGameState(function (data) {
    punkte = data[0]
})
// ... und statt "Verbunden" jedes Bild selbst zeichnen
jacdac.multiplayer.setGuestRenderer(function (target) {
    target.fill(9)
    target.printCenter("Punkte: " + punkte, 50, 1)
})
```

Mit `jacdac.multiplayer.isGuest()` erfährst du, ob dein Gerät gerade Gast ist. Gerechnet wird immer auf dem Host; der Gast schickt weiterhin nur seine Tasten.

## Technischer Hintergrund

Jacdac ist ein Bus über ein einzelnes Datenkabel (beim Calliope mini V3 am Pin **P12**). Jedes Gerät meldet sich dort regelmäßig an. Der Controller sendet seinen kompletten Tastenzustand alle 100 Millisekunden und zusätzlich sofort bei jeder Änderung, inklusive eines Zählers für jeden Tastendruck. So geht auch ein sehr kurzer Druck nicht verloren, wenn einmal ein Datenpaket auf dem Kabel verschluckt wird.

## Siehe auch

[Controller](/reference/controller),
[Jacdac](https://jacdac.github.io/jacdac-docs/)

```package
jacdac-multiplayer
```
