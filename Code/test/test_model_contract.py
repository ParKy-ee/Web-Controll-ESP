"""Regression checks for MQTT -> browser CSV -> train/inference parity."""
import csv
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

import numpy as np

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import imu_features
import serve_model
import train_model


class ModelContractTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.rows = json.loads(subprocess.check_output(['node', '--preserve-symlinks', '--preserve-symlinks-main',
                                                     str(ROOT/'test/test_imu_protocol.cjs'), '--emit']))
        serve_model.create_app(ROOT/'models/pose_model/model_bundle.joblib')
        cls.client = serve_model.app.test_client()

    def test_browser_csv_training_and_prediction_use_identical_features(self):
        rows = [{**r, 'action_label':'wrist_neutral', 'phase_label':'',
                 'subject_id':'test', 'session_id':'s1', 'trial_id':'t1'} for r in self.rows]
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder)/'capture.csv'
            with path.open('w', newline='', encoding='utf-8') as stream:
                writer = csv.DictWriter(stream, fieldnames=list(rows[0]))
                writer.writeheader()
                writer.writerows(rows)
            loaded = train_model.read_recording(path, train_model.load_config(train_model.DEFAULT_CONFIG))
        self.assertEqual(loaded[0]['seq'], 0)
        train_windows = imu_features.samples_to_windows(loaded, 1, 20)
        self.assertEqual(len(train_windows), 1)
        expected = train_windows[0]['features']
        captured = {}
        def capture(features, *args, **kwargs):
            captured.update(features)
            return {'action':'test', 'stage':'unknown'}
        with patch.object(serve_model, 'predict_from_features', side_effect=capture):
            response = self.client.post('/api/predict-window', json={
                'rows': self.rows, 'mount_site':'wrist', 'body_part':'wrist'})
        self.assertEqual(response.status_code, 200)
        self.assertEqual(set(expected), set(captured))
        np.testing.assert_allclose([expected[k] for k in sorted(expected)],
                                   [captured[k] for k in sorted(expected)], rtol=1e-12, atol=1e-12)
        self.assertEqual(len(serve_model.bundle['feature_columns']), 40)

    def test_missing_gyro_and_invalid_samples_are_rejected(self):
        for change, reason in [({'gyro_x':None}, 'invalid_window'), ({'valid':False}, 'invalid_sensor_sample')]:
            rows = [dict(r) for r in self.rows]
            rows[10].update(change)
            result = self.client.post('/api/predict-window', json={
                'rows':rows, 'mount_site':'wrist', 'body_part':'wrist'}).get_json()
            self.assertTrue(result['reason'].startswith(reason), result)

    def test_actual_bundle_prediction_and_unsupported_context(self):
        result = self.client.post('/api/predict-window', json={
            'rows':self.rows, 'mount_site':'wrist', 'body_part':'wrist'}).get_json()
        self.assertIn(result['action'], set(serve_model.bundle['action_labels']) | {'unknown'})
        unsupported = [{**r, 'mount_site':'neck', 'body_part':'neck'} for r in self.rows]
        result = self.client.post('/api/predict-window', json={
            'rows':unsupported, 'mount_site':'neck', 'body_part':'neck'}).get_json()
        self.assertEqual(result['reason'], 'unsupported_site_or_body_part')

    def test_gateway_si_values_and_legacy_g_values(self):
        payload = {'ax':0, 'ay':0, 'az':9.80665, 'gx':0.1, 'gy':0.2, 'gz':0.3, 'accel_unit':'m/s^2'}
        self.assertEqual(serve_model.normalize_live(payload, 'wrist','wrist','unknown')['accel_z'], 9.80665)
        payload.update(az=1, accel_unit='g')
        self.assertEqual(serve_model.normalize_live(payload, 'wrist','wrist','unknown')['accel_z'], 9.80665)

    def test_gaps_rejected_by_training_and_inference(self):
        rows = [{**r, 'action_label':'wrist_neutral', 'phase_label':''} for r in self.rows]
        rows[10]['seq'] += 1
        self.assertEqual(imu_features.samples_to_windows(rows, 1, 20), [])
        result = self.client.post('/api/predict-window', json={
            'rows':rows, 'mount_site':'wrist', 'body_part':'wrist'}).get_json()
        self.assertEqual(result['reason'], 'window_crosses_gap_or_stream_boundary')

    def test_health_monitor_and_local_cors(self):
        health = self.client.get('/api/health', headers={'Origin':'null'})
        self.assertEqual(health.headers['Access-Control-Allow-Origin'], 'null')
        self.assertEqual(len(health.get_json()['feature_columns']), 40)
        for url in ('/monitor', '/imu_protocol.js'):
            with self.client.get(url) as response:
                self.assertEqual(response.status_code, 200)


if __name__ == '__main__':
    unittest.main()
