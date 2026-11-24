# Workshop Link

Workshop Link lets a Music Thing Workshop Computer card, connected to the FM1's USB port with the Computer as USB host, show a live readout on the FM1 screen and read the FM1 encoders and buttons back. Plain USB MIDI between the two works with any firmware. This page describes the FM1 side. The card side, the example card and the hardware notes (power, enumeration, audio feasibility) are in the Workshop_Computer repository under `Demonstrations+HelloWorlds/PicoSDK/ComputerCard/examples/fm1_link/`.

## Using it

- **HOME + ARP** opens the link page, and **HOME** leaves it. A card can also ask for the page when it connects. The FM1 only follows that request from the HOME overview, never over an editor or while recording.
- On the link page, the encoders and every button except HOME are sent to the card. The keys still play, and PLAY and the HOME shortcuts (stop all, undo, tap tempo, Bluetooth) keep their usual meaning.
- If the card stops sending for 3 seconds, the page keeps the last values and shows **LINK LOST** in amber. The session resumes on its own when the card comes back.

## Code

| File | Role |
|---|---|
| `app/WorkshopLink.h` | Protocol: framing, 8→7-bit packing, checksum, message layout. MIT. **Keep identical** to `ComputerCard/WorkshopLink.h` in Workshop_Computer. |
| `app/workshop_link.c`, `app/workshop_link.h` | Session state. The routing task only copies frames into an 8-slot inbox. Decoding, replies and state run on the control task, with no Flash, engine or audio access. |
| `app/link_page.inc` | The page, drawn by `screen.c` with its existing font and band painter. Each widget repaints only when its contents change. |
| `app/transfer.c` | `fm1_app_sysex` gives `F0 7D 'W' 'L'` frames to the link before the transfer protocol's own checks. |
| `app/screen.c` | `LINK` page, HOME + ARP, the show request, and forwarding encoders and buttons. |

Link frames are at most 119 bytes, within the SDK's 128-byte output limit. Replies go back on the port the frame arrived on, so the link also works over BLE MIDI or DIN, although only USB is tested.

### Extending

Host commands `0x20–0x3F` and device commands `0x60–0x7F` are reserved for card- or firmware-specific messages. The `default:` case in `receive()` in `workshop_link.c` is where a firmware handles them. Use them before changing the protocol version.

## Testing

The browser simulator compiles these files as well. In Workshop_Computer, `examples/fm1_link/host_test/run.sh /path/to/fm1_open` builds the simulator natively and runs a full session against the real card-side code, including an FM1 restart and a lost link. It writes the simulated screen to an image.
