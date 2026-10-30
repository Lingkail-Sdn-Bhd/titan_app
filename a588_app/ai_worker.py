#!/usr/bin/env python3
"""Run FYS models on Titan still frames without touching live FYS trip records."""
import argparse
import json
import logging
import os
import time
from datetime import datetime, timezone
from pathlib import Path

import cv2
import numpy as np
from ultralytics import YOLO

LOOKUP = {'big_60_70': 12.130, 'mixed_60_70': 13.940,
          'big_70_80': 13.0575, 'mixed_70_80': 14.4405}


def measure(result, shape):
    height, width = shape[:2]
    unions = [np.zeros((height, width), dtype=bool) for _ in range(2)]
    scores = [[], []]
    if result.masks is None or result.boxes is None:
        return None, 0.0
    for mask, cls, score in zip(result.masks.data.cpu().numpy(),
                                result.boxes.cls.int().cpu().tolist(),
                                result.boxes.conf.cpu().tolist()):
        if cls not in (0, 1):
            continue
        unions[cls] |= cv2.resize(mask, (width, height), interpolation=cv2.INTER_NEAREST) > .5
        scores[cls].append(float(score))
    denominator = int((unions[0] | unions[1]).sum())
    if not denominator or not all(scores):
        return None, 0.0
    return float(unions[0].sum()) / denominator * 100, min(map(max, scores))


def write_result(directory, result):
    tmp = directory / 'result.tmp'
    tmp.write_text(json.dumps(result, allow_nan=False), encoding='utf-8')
    os.replace(tmp, directory / 'result.json')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--captures', type=Path, required=True)
    parser.add_argument('--models', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    logging.basicConfig(level=logging.INFO, format='%(asctime)s %(message)s')
    import torch
    torch.set_num_threads(4)
    segment = YOLO(args.models / 'TruckLoadSeg.pt')
    classifier = YOLO(args.models / 'Tonnage.pt')
    logging.info('Models loaded: segmentation=%s classification=%s', segment.names, classifier.names)
    last = None
    while True:
        files = list(args.captures.glob('*.png'))
        latest = max(files, key=lambda p: p.stat().st_mtime_ns) if files else None
        if latest is None or latest.name == last:
            time.sleep(1)
            continue
        started = time.monotonic()
        try:
            frame = cv2.imread(str(latest))
            if frame is None:
                time.sleep(1)
                continue
            seg = segment.predict(frame, conf=.25, device='cpu', verbose=False)[0]
            coverage, confidence = measure(seg, frame.shape)
            prediction = classifier.predict(frame, device='cpu', verbose=False)[0]
            classification = None
            if prediction.probs is not None:
                label = str(prediction.names[int(prediction.probs.top1)])
                score = float(prediction.probs.top1conf)
                tonnes = int(label) / 1000 if label.isdigit() else LOOKUP.get(label)
                classification = {'label': label, 'confidence': score,
                                  'tonnes': tonnes if score >= .65 else None}
            tonnes = None
            if coverage is not None:
                tonnes = round(4.166741568544499 + .07463574139756685 * coverage
                               + .0005351320020262124 * coverage ** 2, 2)
            annotated = seg.plot()
            text = f'Estimated payload: {tonnes:.2f} t' if tonnes is not None else 'No valid stone + truck bed detection'
            cv2.rectangle(annotated, (0, 0), (annotated.shape[1], 35), (20, 25, 30), -1)
            cv2.putText(annotated, text, (10, 24), cv2.FONT_HERSHEY_SIMPLEX, .55, (230, 240, 240), 1, cv2.LINE_AA)
            image_name = latest.stem + '.jpg'
            temporary = args.output / 'pending.jpg'
            if not cv2.imwrite(str(temporary), annotated):
                raise RuntimeError('Could not save annotated frame')
            os.replace(temporary, args.output / image_name)
            result = {'status': 'detected' if tonnes is not None else 'no_detection',
                      'source': latest.name, 'image': '/detections/' + image_name,
                      'processed_at': datetime.now(timezone.utc).isoformat(),
                      'elapsed_ms': round((time.monotonic() - started) * 1000),
                      'tonnes': tonnes, 'coverage_percent': coverage, 'confidence': confidence,
                      'classification': classification,
                      'method': 'FYS stone / (stone + bed) coverage formula',
                      'note': 'Single-frame estimate; FYS calibration has not been validated for the Titan camera.'}
            write_result(args.output, result)
            last = latest.name
            logging.info('source=%s status=%s tonnes=%s duration=%sms', last, result['status'], tonnes, result['elapsed_ms'])
            for old in sorted(args.output.glob('*.jpg'), key=lambda p: p.stat().st_mtime, reverse=True)[10:]:
                old.unlink(missing_ok=True)
        except Exception:
            logging.exception('Frame inference failed: %s', latest)
            write_result(args.output, {'status': 'error', 'source': latest.name,
                                      'message': 'AI processing failed; check the AI service logs.'})
            time.sleep(5)


if __name__ == '__main__':
    main()
