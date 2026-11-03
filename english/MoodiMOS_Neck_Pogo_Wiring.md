# Magnetic pogo neck wiring — torso ↔ head

Use the [neck diagram](MoodiMOS_Wiring_neck_pogo.svg) together with the
[overall wiring diagram](MoodiMOS_Wiring_torso_head.svg). Pin numbers below are physical connector
positions, not ESP32 GPIO numbers. Keep the robot unplugged while wiring.

## Five-contact map

| Contact | Signal | Torso side: 2×16 connector, spring contacts | Head side: 12-pin connector | Torso wire | Supplied head cable wire |
|---|---|---|---|---|---|
| 1 | +5 V | pin 2, 5 V | pin 2, VBus | red | **red**, second wire |
| 2 | T→H | pin 27, TX GPIO43 | pin 10, RXD GPIO44 | violet | **blue**, tenth wire |
| 3 | H→T | pin 25, RX GPIO44 | pin 9, TXD GPIO43 | light blue | **green**, ninth wire |
| 4 | DOCK | PCF8574 P1, internal pull-up | connected to GND, pin 5 | green | **black**, fifth wire |
| 5 | GND | pin 29, or pin 4/30 | pin 1, GND | black | **black**, first wire |

TX connects to RX at the other end. The torso colours are suggestions; the head colours describe
the supplied cable. Identify every wire by continuity and connector position before soldering.

## The supplied 12-wire head cable

In pin order, 1 to 12:

1. Black — GND.
2. Red — VBus.
3. Green — D−.
4. Yellow — D+.
5. Black — GND.
6. Red — 3V3.
7. Brown — SCL.
8. Orange — SDA.
9. Green — TXD.
10. Blue — RXD.
11. Violet — NC.
12. Grey — IO0.

**Three colours occur twice.** The red wires on pins 2 and 6 are different voltages: mixing them
would put 5 V on a 3.3 V connection. The green wires on pins 3 and 9 are USB D− and serial TXD.
Both black wires are ground. Insulate each unused wire separately, especially 3V3 and the USB pair.
The seven unused signals are D−, D+, 3V3, SCL, SDA, NC and IO0.

## Connector and materials

Use a magnetic five-contact pogo pair at **2.5 mm pitch**, rated at least **1 A per contact**,
with one spring-contact half and one pad half. The reference connector, Sylen B0H22943X6, was
measured with a ruler at about 25 × 4 × 4 mm including its ears: 2 mm on the magnet side,
1 mm for the ears and 1 mm on the solder side. Magnets are integrated at both ends.
No separate neck magnets are fitted in the current design.

Use flexible 26–28 AWG silicone wire in five colours, heat-shrink tubing, Kapton tape and solder.
Estimated lengths are **150 mm** from the torso collar to its 2×16 connector and **60 mm** on the
head side. Measure your actual route before cutting; leave slack without making a trapped loop.

Each connector half enters **from inside** the printed part. Its nose, approximately 20 × 4 × 2 mm,
passes through a **21.4 × 4.6 mm** slot in a **2 mm** skin, with the face flush. The ears bear against
the inside of the skin. The head has a **26.4 mm** channel leading from the central plate opening;
the torso connector enters the cavity below the **Ø 30 mm** collar.
Use `cad/MoodiMOS_Organic/print_stl/pogo_gauge.stl` to check the fit. The 13/10/2026 trial at
4.4 × 20.6 / 25.6 mm was slightly too tight; confirm the fit with your own connector.

## Assembly and electrical checks

Follow Steps 7 and 8 of the build guide on Instructables: identify the matching halves and their
orientation, fit them dry, prepare and insulate the solder joints, then check every contact before
powering the assembled robot. Do not let the spring-contact half slide onto unrelated contacts.

Check continuity for all five paths and verify that +5 V is not shorted to ground or a signal.
The DOCK contact goes to PCF8574 P1 on the torso and ground on the head. It is a presence input,
not a power line. The serial link uses 3.3 V logic: never connect contact 1 (+5 V) to a signal.

The head stays on the neck in this USB-powered version. For its initial programming through its
own USB port, remove it from the neck to avoid simultaneous supplies and serial conflicts.
After both firmwares are installed, update the closed head through the torso with
`firmware/flash_head_neck.sh`. The head ROM also emits startup data on GPIO43; the torso rejects
non-protocol bytes using its frame checks and CRC.
