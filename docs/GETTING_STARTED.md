# Get started with San Audisco

[Download San Audisco](https://github.com/apopov04/San-Audisco/releases/tag/v0.4.0) for each computer. You don't need the source-code download or any developer tools.

## 1. First-time audio setup

San Audisco needs a few audio components so your other apps can send and receive sound. Install them once on each computer. If they are already working, keep them.

| Computer | Install from the official website |
| --- | --- |
| Mac | [BlackHole](https://existential.audio/blackhole/) — both **2ch** and **16ch** |
| Windows | [VB-CABLE](https://vb-audio.com/Cable/) and [Hi-Fi Cable](https://vb-audio.com/Cable/index.htm#DownloadASIOBridge) |

Follow their installer instructions, approve the normal administrator prompts, and restart if requested. On Windows, set both Hi-Fi Cable endpoints to **stereo, 48,000 Hz** in Sound Settings. Leave “Listen to this device” off.

The public ZIPs contain San Audisco itself. They do not contain the separate full Setup installer or download these components automatically.

## 2. Open the app

- **Windows:** extract all the ZIP's contents into a permanent folder. Open **SanAudisco.exe** and keep the other files beside it.
- **Mac:** extract the ZIP, move **San Audisco.app** into Applications, and open it.

If you are updating, quit the old app first and replace it. Use the same version on both computers; keep your existing audio components and settings.

This preview is not notarized on Mac and is unsigned on Windows, so the system may show an app-verification or publisher prompt. Review the prompt without turning off system security. Allow microphone/local-network access on Mac and private-network access on Windows when requested.

## 3. Connect your computers

1. Put both computers on the same Wi-Fi or local network and open San Audisco on each.
2. In **Room settings**, create a room on one computer.
3. Enter its eight-character room code on the other. The invitation lasts five minutes.
4. Wait for **Connected**. The room is remembered for future sessions.

## 4. Choose where you listen and speak

The **Output Hub** is the computer your headphones or speakers are connected to. Select its physical **Output**.

The **Input Hub** is the computer with the microphone you want to use. Select its physical **Input**. It can be a different computer from the Output Hub.

Press **Autoconfigure**, start audio, and explicitly unmute when you're ready. For example, choose your gaming PC as the Output Hub and your MacBook as the Input Hub: hear both computers in the PC's headphones while speaking through the MacBook microphone.

## Common questions

**Do my headphones need to connect to both computers?**

No. Connect them to your Output Hub as usual. Choosing a different Output Hub changes where sound plays; it doesn't move a Bluetooth connection.

**What if Discord or a game still uses the wrong device?**

Apps with their own audio-device settings can override the system's choice. These settings are an example for Discord on a Mac and a game on Windows:

| App | Speaker / output | Microphone / input |
| --- | --- | --- |
| Discord on Mac | BlackHole 2ch | BlackHole 16ch |
| Game or voice app on Windows | CABLE Input | Hi-Fi Cable Output |

**Why can't anyone hear me?**

Check the selected Input Hub and microphone, then check mute in both San Audisco and your voice app. San Audisco starts or reconnects with the microphone muted; unmute once the connection is ready. It doesn't change Discord's mute buttons or bypass a game's push-to-talk setting.

**How do I finish without deleting my room?**

Use **Stop audio** to stop the audio session, or **Quit San Audisco** to stop it and exit on that computer. Your pairing is retained. Closing the window alone leaves audio running in the menu bar or notification area. Normal audio is restored when the original devices are available; if a device was unplugged, select an available physical output.

**The computers can't find each other.**

Check that they're on the same network and that local/private-network access is allowed. Guest Wi-Fi can prevent devices from connecting. Room settings also offers an optional address field if automatic discovery doesn't work.

**Will there be a delay?**

A small network delay is normal. Its size depends on the connection and devices. Keep the game on the computer you're listening from when responsiveness matters most.

[Back to San Audisco](../README.md) · [Report a problem](https://github.com/apopov04/San-Audisco/issues)
