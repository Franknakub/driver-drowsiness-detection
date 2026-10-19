"""Live viewer for the ESP32 camera: place the eye box and collect eye crops.

    py -3.10 ml/eye_viewer.py [--port COM4]

Mouse : drag a square around one eye to set the eye box on the board
Keys  : o = save current crop as open, c = save as closed (ml/data/own/<label>/)
        q = quit
"""

import argparse
import sys
import threading
import time
from pathlib import Path

try:
    import serial
    import serial.tools.list_ports
except ImportError:
    # pyserial is pure Python; borrow the copy that ships with PlatformIO
    sys.path.append(str(Path.home() / ".platformio" / "penv" / "Lib" / "site-packages"))
    import serial
    import serial.tools.list_ports

import tkinter as tk
from PIL import Image, ImageTk

VIEW_SCALE = 3   # preview 200x150 shown at 600x450
CROP_SCALE = 3   # crop 96x96 shown at 288x288
SAVE_DIR = Path(__file__).resolve().parent / "data" / "own"


def find_port():
    for p in serial.tools.list_ports.comports():
        if "303A" in p.hwid.upper():
            return p.device
    raise SystemExit("ESP32-S3 not found, check the USB cable")


class Reader(threading.Thread):
    """Parses "FRM pw ph crop p\\n" + preview bytes + crop bytes; prints other lines."""

    def __init__(self, ser):
        super().__init__(daemon=True)
        self.ser = ser
        self.latest = None
        self.frame_size = (800, 600)
        self.running = True

    def read_exact(self, n):
        buf = bytearray()
        while len(buf) < n and self.running:
            buf += self.ser.read(n - len(buf))
        return bytes(buf)

    def run(self):
        while self.running:
            line = self.ser.readline()
            if not line:
                continue
            text = line.decode(errors="replace").strip()
            if text.startswith("FRM "):
                try:
                    _, pw, ph, cs, p, fw, fh = text.split()
                    pw, ph, cs, p, fw, fh = int(pw), int(ph), int(cs), float(p), int(fw), int(fh)
                except ValueError:
                    continue
                preview = self.read_exact(pw * ph)
                crop = self.read_exact(cs * cs)
                self.latest = (pw, ph, preview, cs, crop, p)
                self.frame_size = (fw, fh)
            elif text:
                print(text.encode("ascii", "replace").decode())


class Viewer:
    def __init__(self, root, ser, reader):
        self.root, self.ser, self.reader = root, ser, reader
        self.roi = (300, 200, 200, 200)  # firmware default, camera frame coords
        self.drag_start = None
        self.frame = None
        self.saved = {"open": 0, "closed": 0}

        root.title("DrowsyFW eye viewer")
        self.canvas = tk.Canvas(root, width=200 * VIEW_SCALE, height=150 * VIEW_SCALE, bg="black")
        self.canvas.grid(row=0, column=0, rowspan=3)
        self.crop_label = tk.Label(root)
        self.crop_label.grid(row=0, column=1, padx=8)
        self.prob_label = tk.Label(root, font=("Segoe UI", 20, "bold"))
        self.prob_label.grid(row=1, column=1)
        self.info = tk.Label(root, justify="left",
                             text="Drag a square around one eye\no = save open, c = save closed, q = quit")
        self.info.grid(row=2, column=1, padx=8)

        self.canvas.bind("<ButtonPress-1>", self.on_press)
        self.canvas.bind("<B1-Motion>", self.on_drag)
        self.canvas.bind("<ButtonRelease-1>", self.on_release)
        root.bind("<Key>", self.on_key)
        root.protocol("WM_DELETE_WINDOW", self.quit)
        self.tick()

    def send(self, cmd):
        self.ser.write((cmd + "\n").encode())

    def to_frame(self, x, y):
        fw, fh = self.reader.frame_size
        return int(x * fw / (200 * VIEW_SCALE)), int(y * fh / (150 * VIEW_SCALE))

    def square_from(self, x0, y0, x1, y1):
        side = max(abs(x1 - x0), abs(y1 - y0), 8)
        return min(x0, x1), min(y0, y1), side

    def on_press(self, e):
        self.drag_start = (e.x, e.y)

    def on_drag(self, e):
        if self.drag_start:
            x, y, side = self.square_from(*self.drag_start, e.x, e.y)
            self.canvas.delete("drag")
            self.canvas.create_rectangle(x, y, x + side, y + side, outline="yellow", width=2, tags="drag")

    def on_release(self, e):
        if not self.drag_start:
            return
        x, y, side = self.square_from(*self.drag_start, e.x, e.y)
        self.drag_start = None
        self.canvas.delete("drag")
        sx, sy = self.to_frame(x, y)
        sw, _ = self.to_frame(side, side)
        self.roi = (sx, sy, sw, sw)
        self.send("roi %d %d %d %d" % self.roi)

    def on_key(self, e):
        key = e.char.lower()
        if key == "q":
            self.quit()
        elif key in ("o", "c") and self.frame:
            label = "open" if key == "o" else "closed"
            _, _, _, cs, crop, _ = self.frame
            out = SAVE_DIR / label
            out.mkdir(parents=True, exist_ok=True)
            Image.frombytes("L", (cs, cs), crop).save(out / f"{label}_{time.time_ns()}.png")
            self.saved[label] += 1
            self.info.config(text=f"Saved open {self.saved['open']} / closed {self.saved['closed']}\n"
                                  "o = save open, c = save closed, q = quit")

    def tick(self):
        frame = self.reader.latest
        if frame and frame is not self.frame:
            self.frame = frame
            pw, ph, preview, cs, crop, p = frame
            img = Image.frombytes("L", (pw, ph), preview).resize((pw * VIEW_SCALE, ph * VIEW_SCALE))
            self.view_img = ImageTk.PhotoImage(img)
            self.canvas.delete("img")
            self.canvas.create_image(0, 0, anchor="nw", image=self.view_img, tags="img")
            self.canvas.tag_lower("img")

            # Current eye box, colored by the model's answer
            color = "red" if p >= 0.5 else "lime"
            k = (200 * VIEW_SCALE) / self.reader.frame_size[0]
            x, y, w, h = self.roi
            self.canvas.delete("roi")
            self.canvas.create_rectangle(x * k, y * k, (x + w) * k, (y + h) * k,
                                         outline=color, width=2, tags="roi")

            crop_img = Image.frombytes("L", (cs, cs), crop).resize((cs * CROP_SCALE, cs * CROP_SCALE),
                                                                   Image.NEAREST)
            self.crop_imgtk = ImageTk.PhotoImage(crop_img)
            self.crop_label.config(image=self.crop_imgtk)
            state = "CLOSED" if p >= 0.5 else "OPEN"
            self.prob_label.config(text=f"{state}  P(closed) {p:.2f}", fg=color)
        self.root.after(30, self.tick)

    def quit(self):
        try:
            self.send("stream 0")
        except serial.SerialException:
            pass
        self.reader.running = False
        self.root.destroy()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port")
    args = ap.parse_args()

    port = args.port or find_port()
    # dtr/rts off: toggling them on native USB can reset the ESP32-S3
    ser = serial.Serial()
    ser.port, ser.baudrate, ser.timeout = port, 115200, 0.5
    ser.dtr = False
    ser.rts = False
    ser.open()
    print(f"Connected to {port}")
    ser.write(b"stream 1\n")

    reader = Reader(ser)
    reader.start()
    root = tk.Tk()
    Viewer(root, ser, reader)
    root.mainloop()
    ser.close()


if __name__ == "__main__":
    main()
