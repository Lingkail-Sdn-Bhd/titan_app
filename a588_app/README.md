# A588 Titan camera monitor and AI tonnage

A small Ubuntu app for the Titan Board Mini camera. It displays the newest
capture, annotated stone/truck-bed detections, estimated payload tonnage,
confidence, processing time, and a clock with seconds. Pictures refresh every
three seconds. The AI worker processes each newest available frame on the CPU;
intermediate frames are skipped if inference falls behind.

## Use the deployed app

On the office LAN, open http://192.168.101.125:8080/ in a browser.
Use **Download picture** for the original frame and **Open annotated detection**
for the most recently processed image. No detection means no valid segmentation
estimate for that frame. The classifier candidate is listed separately and may
be below its confidence threshold. Results older than 45 seconds show as stale.

## Requirements

- Ubuntu ARM64 A588/RK3588, Python 3.10, systemd, and internet for package setup.
- Titan board uploading RGB565 frames to `POST /upload` on TCP 5000.
- Two existing FYS weights: `TruckLoadSeg.pt` and `Tonnage.pt`.
- LAN access to TCP 8080 for the viewer. This app has no login; use a trusted LAN.

Model weights, captures, credentials, virtual environments, and generated
annotations are not committed. The deployed FYS models are currently on the
field RK3588 at `/home/ztl/QuarryCommandCenter/models/`. Obtain those files
through your existing SSH access; do not substitute unrelated model weights.
The segmentation class map must be `0: stone_load, 1: truck_bed`.

## Deploy on A588

These commands target the current account `ztl` and directory
`/home/ztl/Desktop/lorry`. If deploying under a different account or directory,
update `User`, `WorkingDirectory`, `ExecStart`, and `YOLO_CONFIG_DIR` in all three
service files before installing them. SSH passwords are entered interactively.

### 1. Get the source and install the runtime

Run on A588:

```bash
sudo apt-get update
sudo apt-get install -y git python3.10-venv python3-pil

git clone --branch codex/a588-ai-tonnage-viewer https://github.com/Lingkail-Sdn-Bhd/titan_app.git
cd titan_app
APP_DIR=/home/ztl/Desktop/lorry/viewer
mkdir -p "$APP_DIR/models" "$APP_DIR/detections" "$APP_DIR/.ultralytics"
mkdir -p /home/ztl/Desktop/lorry/captures_http
cp a588_app/*.py a588_app/index.html a588_app/requirements.txt "$APP_DIR/"
cp Titan_Board_Lorry_Capture/script/receive_face_http.py /home/ztl/Desktop/lorry/

python3 -m venv "$APP_DIR/.venv"
"$APP_DIR/.venv/bin/pip" install -r "$APP_DIR/requirements.txt"
"$APP_DIR/.venv/bin/pip" check
```

The pinned PyTorch 2.6 ARM64 wheel runs on the CPU. No NVIDIA GPU or RKNN
conversion is required. `requirements.txt` is intended for this ARM64 deployment.

### 2. Supply the models

Copy the existing FYS weights to:

```text
/home/ztl/Desktop/lorry/viewer/models/TruckLoadSeg.pt
/home/ztl/Desktop/lorry/viewer/models/Tonnage.pt
```

For example, from a computer that already has both model files:

```bash
scp TruckLoadSeg.pt Tonnage.pt ztl@192.168.101.125:/home/ztl/Desktop/lorry/viewer/models/
```

### 3. Enable automatic startup

Run from the cloned repository on A588:

```bash
sudo install -m 644 a588_app/titan-camera-receiver.service /etc/systemd/system/
sudo install -m 644 a588_app/titan-camera-viewer.service /etc/systemd/system/
sudo install -m 644 a588_app/titan-camera-ai.service /etc/systemd/system/
sudo systemctl daemon-reload
```

If a manually started receiver is already using port 5000, stop that process
before starting the receiver service. Do not run two receivers on the same port.
On the current office A588 these services already exist; restart them after
copying an update:

```bash
sudo systemctl enable --now titan-camera-receiver titan-camera-viewer titan-camera-ai
sudo systemctl restart titan-camera-receiver titan-camera-viewer titan-camera-ai
systemctl is-active titan-camera-receiver titan-camera-viewer titan-camera-ai
```

### 4. Send frames and verify

The current Titan Mini firmware targets `192.168.0.59:5000`, A588's office Wi-Fi
address. A588 is also reachable at `192.168.101.125` over Ethernet. Check
`hostname -I`. If changing the board destination, edit `UPLOAD_HOST` and
`UPLOAD_PORT` in `Titan_Mini_Lorry_Capture/src/hal_entry.c`, then rebuild and flash.
The board and the target address must be reachable on the network.

```bash
curl -fsS http://127.0.0.1:8080/api/latest
curl -fsS http://127.0.0.1:8080/api/result
journalctl -u titan-camera-ai.service -n 30 --no-pager
```

Open `http://<A588-IP>:8080/`. Allow inbound TCP 5000 from the board and TCP 8080
from viewer clients if a firewall is enabled. The receiver's `/upload` endpoint
accepts POSTs; opening it in a browser with GET is not a health check.

## Operations and troubleshooting

```bash
# Follow inference logs
journalctl -u titan-camera-ai.service -f
# Check all services
systemctl status titan-camera-receiver titan-camera-viewer titan-camera-ai --no-pager
# Restart inference after changing weights or worker code
sudo systemctl restart titan-camera-ai
# Stop the app, if required
sudo systemctl stop titan-camera-ai titan-camera-viewer titan-camera-receiver
```

- No pictures: check board power, destination IP, routing, receiver logs, and port
  5000. Original PNGs live in `/home/ztl/Desktop/lorry/captures_http/`.
- Starting AI or processing error: check AI logs, package installation, model
  paths, and file ownership. The worker runs as `ztl`.
- No valid detection: both stone and truck bed must be found. Check camera
  framing and lighting; not every video frame contains a loaded truck.
- Low classifier confidence: the candidate is shown, but its tonnage is withheld
  below 65%. It does not replace the segmentation estimate.
- Stale result: compare capture and AI timestamps and inspect worker logs.
- Original captures are retained indefinitely; monitor disk usage and archive
  old PNGs as needed. Only the latest ten annotated JPEGs are kept.

## Manual run (without systemd)

Stop the corresponding services first to avoid duplicate workers or port conflicts.
Run each command in its own terminal from `/home/ztl/Desktop/lorry/viewer`:

```bash
python3 ../receive_face_http.py --out ../captures_http --port 5000
python3 app.py --captures ../captures_http --port 8080
.venv/bin/python ai_worker.py --captures ../captures_http --models models --output detections
```

## Files and endpoints

- `app.py`, `index.html`: HTTP viewer and browser UI.
- `ai_worker.py`: independent local FYS inference; no live trip API calls.
- `requirements.txt`: pinned model runtime versions.
- `*.service`: receiver, viewer, and AI startup definitions.
- `GET /api/latest`: newest original frame and capture count.
- `GET /api/result`: last inference result, source filename, and annotation URL.
- `GET /captures/<name>.png`: original capture.
- `GET /detections/<name>.jpg`: annotated image.

## Estimates

The primary estimate uses the FYS segmentation convention: class 0 is stone,
class 1 is truck bed. Both must be detected at confidence >= 0.25. Stone coverage
is `100 * stone area / union(stone, bed) area`. Estimated tonnes are:

`4.166741568544499 + 0.07463574139756685*c + 0.0005351320020262124*c*c`

The classifier also reports its label and confidence. Numeric labels are kg;
legacy class labels use the FYS lookup. Classifier tonnage is withheld below 65%
confidence. The primary tonnage is never replaced by the classifier result.

These are single-frame experimental estimates. The FYS formula's accuracy has
not been validated for the Titan camera geometry. No live trip API is called.
The viewer shows the source frame, inference timestamp, and stale results.
The image annotation is shown in the main view when it matches the latest frame;
the annotated detection link always opens the last processed frame.

Inspect logs using `journalctl -u titan-camera-ai.service -f`.
