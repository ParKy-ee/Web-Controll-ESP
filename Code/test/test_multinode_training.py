"""Exercise browser multi-node CSV through the real training reader and extractor."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from train_model import load_config, load_windows, read_recording


class MultiNodeTrainingTest(unittest.TestCase):
    def test_separate_20hz_csv_files_produce_separate_training_windows(self):
        files = json.loads(subprocess.check_output([
            'node', '--preserve-symlinks', '--preserve-symlinks-main',
            str(ROOT / 'test/test_multinode_recorder.cjs'), '--emit'], encoding='utf-8'))
        config = load_config(ROOT / 'config/pose_labels.json')
        with tempfile.TemporaryDirectory() as folder:
            paths = []
            for item in files:
                path = Path(folder) / item['name']
                path.write_text(item['csv'], encoding='utf-8')
                paths.append(path)
                rows = read_recording(path, config)
                self.assertEqual(len(rows), 41)
                self.assertEqual(len({r['sensor_uid'] for r in rows}), 1)
                self.assertEqual(rows[-1]['device_time_s'] - rows[0]['device_time_s'], 2)
                self.assertTrue(all(r['phase_label'] == '' for r in rows), 'unreviewed timer cues are not phase ground truth')
                self.assertTrue(all(r['valid'] for r in rows))
            windows, ignored = load_windows(paths, config, 1.0)
            self.assertEqual(ignored, [])
            self.assertTrue(windows)
            self.assertEqual({r['sensor_uid'] for r in windows}, {'neck-uid', 'lumbar-uid'})
            self.assertEqual({r['body_part'] for r in windows}, {'neck', 'lumbar'})
            self.assertEqual({r['action_label'] for r in windows}, {'neck_neutral', 'lumbar_neutral'})
            # Explicit discontinuities survive CSV ingestion, even if clocks and counters look contiguous.
            text = files[0]['csv']
            import csv
            import io
            data = list(csv.DictReader(io.StringIO(text.lstrip('\ufeff'))))
            data[10]['gap_before'] = 'true'
            path = paths[0]
            with path.open('w', encoding='utf-8', newline='') as output:
                writer = csv.DictWriter(output, fieldnames=data[0].keys())
                writer.writeheader()
                writer.writerows(data)
            self.assertTrue(read_recording(path, config)[10]['gap_before'])


if __name__ == '__main__':
    unittest.main()
