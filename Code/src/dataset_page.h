#pragma once

#include <Arduino.h>

static const char DATASET_PAGE[] PROGMEM = R"PLANKOHTML(
<!doctype html>
<html lang="th">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <title>PlanKO IMU Dataset Recorder</title>
  <style>
    :root{color-scheme:dark;--bg:#0b1219;--panel:#14222c;--line:#2b414e;--text:#edf5f8;--muted:#a9bbc4;--mint:#74f0c2;--red:#ff9292}
    *{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--text);font:15px system-ui,sans-serif}
    main{max-width:1120px;margin:auto;padding:24px 16px 48px}h1,h2,p{margin-top:0}h1{font-size:25px;margin-bottom:6px}h2{font-size:18px;margin-bottom:12px}
    .muted,.hint{color:var(--muted)}.hint{font-size:13px;line-height:1.5}.topline{display:flex;justify-content:space-between;gap:12px;align-items:center;margin-bottom:18px}
    .badge{padding:6px 11px;border-radius:999px;background:#253440;color:var(--muted);font-size:12px}.badge.online{background:#153c32;color:var(--mint)}.card,.node{background:var(--panel);border:1px solid var(--line);border-radius:14px;padding:16px;margin:14px 0}
    .grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(180px,1fr));gap:10px}.field{display:grid;gap:6px;color:var(--muted);font-size:12px}.field input,.field select{width:100%;min-height:40px;padding:8px 10px;background:#0d171f;color:var(--text);border:1px solid var(--line);border-radius:8px;font:inherit}
    button{border:1px solid var(--line);border-radius:8px;background:#1c303c;color:var(--text);padding:9px 13px;font:inherit;cursor:pointer}button:disabled{opacity:.45;cursor:default}.primary{background:#17634f;border-color:#26836b}.danger{background:#542a31;border-color:#82424a}.controls{display:flex;flex-wrap:wrap;gap:8px;margin-top:12px;align-items:center}.phase.active{outline:2px solid var(--mint);color:var(--mint)}
    .node-head{display:flex;justify-content:space-between;gap:10px;align-items:center}.node-head strong{font-size:16px}.node-status{font-size:12px;color:var(--muted)}.node-status.online{color:var(--mint)}.node-grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(170px,1fr));gap:9px;margin-top:12px}.node-foot{margin-top:10px;color:var(--muted);font-size:12px}.feedback{min-height:20px;margin:10px 0 0;color:var(--muted);font-size:13px}.links{display:grid;gap:7px;margin-top:12px}.links a{color:var(--mint);overflow-wrap:anywhere}.small{font-size:12px}.recording{color:var(--red)}
    .guided{margin-top:14px;padding:12px;border:1px solid var(--line);border-radius:10px;background:#101c27}.guided-steps{display:grid;gap:7px;margin-top:9px}.guided-step{display:grid;grid-template-columns:minmax(130px,1fr) minmax(130px,.6fr);gap:8px;align-items:center}.guided-step select,.guided-step input,.review-row select,.review-row input[type=number]{min-height:36px;width:100%;padding:6px 9px;background:#0d171f;color:var(--text);border:1px solid var(--line);border-radius:8px;font:inherit}.review{display:grid;gap:7px;margin-top:10px}.review-row{display:grid;grid-template-columns:minmax(100px,.5fr) minmax(140px,1fr) auto;gap:8px;align-items:center}.review-row label{display:flex;gap:5px;align-items:center;color:var(--muted);font-size:12px}
    @media(max-width:560px){main{padding:18px 11px 36px}.topline{align-items:flex-start;flex-direction:column}.card,.node{padding:13px}.review-row{grid-template-columns:minmax(80px,.5fr) 1fr}.review-row label{grid-column:1/-1}}
  </style>
</head>
<body>
<main>
  <div class="topline"><div><h1>PlanKO · เก็บข้อมูล IMU</h1><div class="muted">Gateway รับข้อมูลจริงจาก Node ผ่าน BLE และแยก CSV ตาม Node ID</div></div><span id="gatewayStatus" class="badge">กำลังเชื่อมต่อ…</span></div>
  <p id="networkInfo" class="hint"></p>

  <section class="card">
    <h2>ข้อมูลชุดทดลอง</h2>
    <div class="grid">
      <label class="field">ผู้เข้าร่วม<input id="subjectId" value="P01" maxlength="40"></label>
      <label class="field">Session<input id="sessionId" maxlength="60"></label>
      <label class="field">Trial<input id="trialId" value="trial-01" maxlength="60"></label>
      <label class="field">ท่าที่บันทึก<input id="actionLabel" maxlength="60" placeholder="เช่น neck_flexion"></label>
    </div>
    <div class="controls" aria-label="phase label">
      <span class="small muted">Phase ปัจจุบัน:</span>
      <button class="phase active" data-phase="neutral" type="button">ปกติ</button>
      <button class="phase" data-phase="approaching" type="button">กำลังเข้าท่า</button>
      <button class="phase" data-phase="holding" type="button">ค้างในท่า</button>
      <button class="phase" data-phase="returning" type="button">กำลังกลับ</button>
      <span id="phaseLabel" class="small">neutral</span>
    </div>
    <div class="guided">
      <label class="small"><input id="guidedEnabled" type="checkbox" checked> ใช้ตัวช่วยเปลี่ยน phase ตามเวลา</label>
      <div class="guided-steps">
        <label class="guided-step"><select class="guided-phase"><option value="neutral">neutral / ปกติ</option><option value="approaching">approaching / กำลังเข้าท่า</option><option value="holding">holding / ค้างในท่า</option><option value="returning">returning / กำลังกลับ</option></select><span><input class="guided-duration" type="number" min="0.5" max="120" step="0.5" value="3" aria-label="ระยะเวลา neutral วินาที"></span></label>
        <label class="guided-step"><select class="guided-phase"><option value="neutral">neutral / ปกติ</option><option value="approaching" selected>approaching / กำลังเข้าท่า</option><option value="holding">holding / ค้างในท่า</option><option value="returning">returning / กำลังกลับ</option></select><span><input class="guided-duration" type="number" min="0.5" max="120" step="0.5" value="3" aria-label="ระยะเวลา approaching วินาที"></span></label>
        <label class="guided-step"><select class="guided-phase"><option value="neutral">neutral / ปกติ</option><option value="approaching">approaching / กำลังเข้าท่า</option><option value="holding" selected>holding / ค้างในท่า</option><option value="returning">returning / กำลังกลับ</option></select><span><input class="guided-duration" type="number" min="0.5" max="120" step="0.5" value="5" aria-label="ระยะเวลา holding วินาที"></span></label>
        <label class="guided-step"><select class="guided-phase"><option value="neutral">neutral / ปกติ</option><option value="approaching">approaching / กำลังเข้าท่า</option><option value="holding">holding / ค้างในท่า</option><option value="returning" selected>returning / กำลังกลับ</option></select><span><input class="guided-duration" type="number" min="0.5" max="120" step="0.5" value="3" aria-label="ระยะเวลา returning วินาที"></span></label>
        <label class="guided-step"><select class="guided-phase"><option value="neutral" selected>neutral / ปกติ</option><option value="approaching">approaching / กำลังเข้าท่า</option><option value="holding">holding / ค้างในท่า</option><option value="returning">returning / กำลังกลับ</option></select><span><input class="guided-duration" type="number" min="0.5" max="120" step="0.5" value="2" aria-label="ระยะเวลา neutral วินาที"></span></label>
      </div>
      <p class="hint">เริ่มตามลำดับด้านบน; เวลาป้ายกำกับเป็น cue เบื้องต้น ให้ตรวจเวลาของแต่ละช่วงก่อนดาวน์โหลด CSV.</p>
    </div>
    <div class="controls">
      <button id="recordStart" class="primary" type="button">เริ่มบันทึกทุก Node</button>
      <button id="recordStop" class="danger" type="button" disabled>หยุดและสร้าง CSV</button>
      <span id="recordCount" class="small muted">ยังไม่บันทึก</span>
    </div>
    <p class="hint">ข้อมูลจะอยู่ในหน่วยความจำของหน้าเว็บระหว่างบันทึก เมื่อหยุด ระบบสร้างไฟล์ CSV แยกให้แต่ละ Node เพื่อดาวน์โหลด ควรตรวจ label และ phase ก่อนนำไปฝึกโมเดล</p>
    <div id="phaseEditor" class="review"></div>
    <div class="controls"><button id="addPhaseBoundary" type="button" disabled>เพิ่มจุดเปลี่ยน phase</button><button id="downloadCsv" class="primary" type="button" disabled>ยืนยัน label และดาวน์โหลด CSV แยกตาม Node</button></div>
    <div id="feedback" class="feedback" role="status">กำลังอ่านสถานะ Gateway…</div>
    <div id="downloads" class="links"></div>
  </section>

  <h2>Node และตำแหน่งติดตั้ง</h2>
  <p class="hint">ค่าเริ่มต้น Node 1 = คอ, Node 2 = หลังส่วนล่าง; เปลี่ยนชื่อ ตำแหน่ง ส่วนร่างกาย และข้างได้ แล้วกดบันทึก</p>
  <div id="nodes"></div>
</main>
<script>
(() => {
  const $ = id => document.getElementById(id);
  const SITE_OPTIONS = ["wrist","neck","chest","upper_back","lumbar","pelvis","head","other"];
  const SITE_LABELS = {wrist:"ข้อมือ",neck:"คอ",chest:"หน้าอก",upper_back:"หลังส่วนบน",lumbar:"หลังส่วนล่าง",pelvis:"เชิงกราน",head:"ศีรษะ",other:"อื่น ๆ"};
  const BODY_OPTIONS = ["neck","back","wrist","chest","other"];
  const BODY_LABELS = {neck:"คอ",back:"หลัง",wrist:"ข้อมือ",chest:"หน้าอก",other:"อื่น ๆ"};
  const PHASE_LABELS = {neutral:"neutral",approaching:"approaching",holding:"holding",returning:"returning"};
  const cursors = {};
  const nodeCache = {};
  const recorders = {};
  const downloadUrls = [];
  let currentPhase = "neutral";
  let phaseEvents = [];
  let guidedTimer = 0;
  let guidedPlan = [];
  let recording = false;
  let pollBusy = false;
  let lastPollAt = 0;

  function escCsv(value) {
    const s = value === null || value === undefined ? "" : String(value);
    return /[",\r\n]/.test(s) ? '"' + s.replace(/"/g, '""') + '"' : s;
  }
  function setFeedback(message, isError) {
    $("feedback").textContent = message;
    $("feedback").style.color = isError ? "var(--red)" : "var(--muted)";
  }
  function setNodeConfigControlsDisabled(disabled) {
    document.querySelectorAll(".node-name,.node-site,.node-body,.node-side,.save-config")
      .forEach(control => control.disabled = disabled);
  }
  function optionsHtml(values, labels) {
    return values.map(v => `<option value="${v}">${labels[v] || v}</option>`).join("");
  }
  function makeNodeCard(node) {
    const card = document.createElement("article");
    card.className = "node";
    card.id = `node-${node.nodeId}`;
    card.innerHTML = `<div class="node-head"><strong></strong><span class="node-status">รอ Node</span></div>
      <div class="node-grid">
        <label class="field">ชื่อที่แสดง<input class="node-name" maxlength="30"></label>
        <label class="field">ตำแหน่งติด IMU<select class="node-site">${optionsHtml(SITE_OPTIONS,SITE_LABELS)}</select></label>
        <label class="field">ส่วนร่างกาย<select class="node-body">${optionsHtml(BODY_OPTIONS,BODY_LABELS)}</select></label>
        <label class="field">ข้าง<select class="node-side"><option value="unknown">ไม่ระบุ</option><option value="left">ซ้าย</option><option value="right">ขวา</option></select></label>
      </div><div class="controls"><button class="save-config" type="button">บันทึกตำแหน่ง</button><span class="small muted config-status"></span></div>
      <div class="node-foot"></div>`;
    card.querySelector(".node-head strong").textContent = `Node ${node.nodeId}`;
    card.querySelector(".node-name").value = node.name || `Node ${node.nodeId}`;
    card.querySelector(".node-site").value = node.mount_site || "other";
    card.querySelector(".node-body").value = node.body_part || "other";
    card.querySelector(".node-side").value = node.side || "unknown";
    card.querySelector(".save-config").addEventListener("click", () => saveNodeConfig(node.nodeId));
    $("nodes").appendChild(card);
    return card;
  }
  function updateNodeCard(node) {
    nodeCache[node.nodeId] = node;
    let card = $(`node-${node.nodeId}`);
    if (!card) card = makeNodeCard(node);
    const status = card.querySelector(".node-status");
    status.textContent = node.connected ? (node.hasData ? "BLE connected · รับข้อมูล" : "BLE connected · รอข้อมูล IMU") : "ออฟไลน์";
    status.classList.toggle("online", !!node.connected && !!node.hasData);
    card.querySelector(".node-foot").textContent = `UID: ${node.sensor_uid || "ยังไม่พบ"} · Seq: ${node.latest_seq || 0} · ตกหล่น: ${node.total_dropped || 0}${recording && recorders[node.nodeId] ? ` · บันทึก ${recorders[node.nodeId].rows.length} samples` : ""}`;
  }
  async function refreshNodeList() {
    const response = await fetch("/api/nodes", {cache:"no-store"});
    if (!response.ok) throw new Error(`Gateway HTTP ${response.status}`);
    const data = await response.json();
    (data.nodes || []).forEach(updateNodeCard);
    $("networkInfo").textContent = `Gateway: ${data.ip || location.host} · BLE nodes: ${data.connectedNodes || 0}/4 · MQTT: ${data.mqtt ? "เชื่อมต่อ" : "ยังไม่เชื่อมต่อ"}`;
    $("gatewayStatus").textContent = "Gateway online";
    $("gatewayStatus").classList.add("online");
  }
  async function saveNodeConfig(nodeId) {
    const card = $(`node-${nodeId}`);
    const button = card.querySelector(".save-config");
    const status = card.querySelector(".config-status");
    button.disabled = true;
    status.textContent = "กำลังบันทึก…";
    try {
      const body = new URLSearchParams({
        node_id:String(nodeId), name:card.querySelector(".node-name").value.trim() || `Node ${nodeId}`,
        mount_site:card.querySelector(".node-site").value, body_part:card.querySelector(".node-body").value,
        side:card.querySelector(".node-side").value
      });
      const response = await fetch("/api/node-config", {method:"POST",headers:{"Content-Type":"application/x-www-form-urlencoded"},body});
      const result = await response.json();
      if (!response.ok) throw new Error(result.message || `HTTP ${response.status}`);
      status.textContent = "บันทึกแล้ว";
      await refreshNodeList();
    } catch (error) {
      status.textContent = `บันทึกไม่สำเร็จ: ${error.message}`;
    } finally { button.disabled = recording; }
  }
  function isSeqAfter(seq, after) {
    const delta = (Number(seq) - Number(after)) & 0xffff;
    return delta > 0 && delta < 0x8000;
  }
  function updatePhase(phase, source = "manual_button") {
    currentPhase = phase;
    $("phaseLabel").textContent = phase;
    document.querySelectorAll(".phase").forEach(button => button.classList.toggle("active", button.dataset.phase === phase));
    if (recording) {
      const elapsedMs = Math.max(0, Date.now() - window.datasetMetadata.started);
      const previous = phaseEvents[phaseEvents.length - 1];
      if (!previous || previous.phase !== phase || elapsedMs !== previous.elapsedMs) {
        phaseEvents.push({elapsedMs, phase, source, reviewed:false});
      }
    }
  }
  function setCaptureControlsDisabled(disabled, guided = false) {
    ["subjectId", "sessionId", "trialId", "actionLabel", "guidedEnabled"].forEach(id => $(id).disabled = disabled);
    document.querySelectorAll(".guided-phase,.guided-duration").forEach(control => control.disabled = disabled);
    document.querySelectorAll(".phase").forEach(button => button.disabled = disabled && guided);
  }
  function readGuidedPlan() {
    const phases = [...document.querySelectorAll(".guided-phase")];
    const durations = [...document.querySelectorAll(".guided-duration")];
    return phases.map((select, index) => ({phase:select.value,durationMs:Math.round(Number(durations[index].value) * 1000)}));
  }
  function runGuidedStep(index) {
    if (!recording) return;
    if (index >= guidedPlan.length) { stopRecording(); return; }
    updatePhase(guidedPlan[index].phase, "guided");
    guidedTimer = window.setTimeout(() => runGuidedStep(index + 1), guidedPlan[index].durationMs);
  }
  function startRecording() {
    const subject = $("subjectId").value.trim();
    const trial = $("trialId").value.trim();
    const action = $("actionLabel").value.trim();
    if (!subject || !trial || !action) { setFeedback("กรอกผู้เข้าร่วม Trial และท่าที่บันทึกก่อนเริ่ม", true); return; }
    const useGuided = $("guidedEnabled").checked;
    const plan = useGuided ? readGuidedPlan() : [];
    if (useGuided && (!plan.length || plan.some(step => !Number.isFinite(step.durationMs) || step.durationMs < 500 || step.durationMs > 120000))) {
      setFeedback("กำหนดเวลาแต่ละ phase ระหว่าง 0.5 ถึง 120 วินาที", true); return;
    }
    const connected = Object.values(nodeCache).filter(node => node.connected);
    if (!connected.length) { setFeedback("ยังไม่มี Node เชื่อมต่อ BLE", true); return; }
    const started = Date.now();
    const session = $("sessionId").value.trim() || new Date(started).toISOString().replace(/[-:.TZ]/g, "").slice(0,14);
    Object.keys(recorders).forEach(key => delete recorders[key]);
    for (const node of connected) {
      const cursor = cursors[node.nodeId];
      const startSeq = cursor && cursor.bootId === Number(node.boot_id) && cursor.afterSeq !== null
        ? cursor.afterSeq : Number(node.latest_seq || 0);
      recorders[node.nodeId] = {rows:[], startedAt:started, startSeq, startBootId:Number(node.boot_id), lastSeq:null, bootId:null, streamEpoch:0, gapPending:false};
    }
    phaseEvents = [];
    guidedPlan = plan;
    $("phaseEditor").replaceChildren();
    $("addPhaseBoundary").disabled = true;
    $("downloadCsv").disabled = true;
    recording = true;
    setNodeConfigControlsDisabled(true);
    setCaptureControlsDisabled(true, useGuided);
    $("recordStart").disabled = true;
    $("recordStop").disabled = false;
    $("recordCount").textContent = `กำลังบันทึก ${connected.length} Node`;
    $("downloads").replaceChildren();
    setFeedback("กำลังบันทึก sample จริงจาก Node ที่เชื่อมต่ออยู่; ถ้า buffer แจ้งว่าข้อมูลตกหล่น CSV จะทำเครื่องหมาย gap_before", false);
    window.datasetMetadata = {subject, session, trial, action, started};
    updatePhase(useGuided ? plan[0].phase : "neutral", useGuided ? "guided" : "manual_button");
    if (useGuided) guidedTimer = window.setTimeout(() => runGuidedStep(1), plan[0].durationMs);
  }
  const CSV_COLUMNS = ["sensor_uid","sensor_id","firmware_sensor_id","node_id","node_name","mount_site","body_part","side","subject_id","session_id","trial_id","action_label","phase_label","phase_annotation_source","phase_annotation_reviewed","phase_elapsed_ms","calibration_id","calibration_method_version","calibration_time_boot_ms","calibration_boot_id","calibrated","relative_angle_valid","relative_orientation_valid","unit_accel","unit_gyro","host_time_iso","host_time_s","host_elapsed_ms","boot_ms","boot_id","seq","stream_epoch","gap_before","raw_accel_x","raw_accel_y","raw_accel_z","raw_gyro_x","raw_gyro_y","raw_gyro_z","accel_x","accel_y","accel_z","gyro_x","gyro_y","gyro_z","corrected_gyro_x","corrected_gyro_y","corrected_gyro_z","roll_deg","pitch_deg","relative_roll_deg","relative_pitch_deg","relative_tilt_deg","yaw_deg","accel_norm_g","gyro_norm_dps","steady","valid","firmware_status"];
  function captureSample(node, sample, hostTimeMs) {
    if (!recording) return;
    if (!recorders[node.nodeId]) recorders[node.nodeId] = {rows:[], startedAt:window.datasetMetadata.started, startSeq:null, startBootId:Number(node.boot_id), lastSeq:null, bootId:null, streamEpoch:0, gapPending:false};
    const rec = recorders[node.nodeId];
    const meta = window.datasetMetadata;
    if (hostTimeMs < meta.started) return;
    const sampleBootId = Number(node.boot_id);
    if (rec.lastSeq === null && rec.startSeq !== null && rec.startBootId === sampleBootId &&
        !isSeqAfter(sample.seq,rec.startSeq)) return;
    if (rec.bootId !== null && rec.bootId !== Number(node.boot_id)) { rec.streamEpoch++; rec.gapPending = true; }
    const startGap = rec.lastSeq === null && rec.startSeq !== null && rec.startBootId === sampleBootId &&
      Number(sample.seq) !== ((rec.startSeq + 1) & 0xffff);
    const bootChangedAtStart = rec.lastSeq === null && rec.startBootId !== null && rec.startBootId !== sampleBootId;
    rec.bootId = sampleBootId;
    const gap = rec.gapPending || startGap || bootChangedAtStart ||
      (rec.lastSeq !== null && Number(sample.seq) !== ((rec.lastSeq + 1) & 0xffff));
    rec.gapPending = false;
    rec.lastSeq = Number(sample.seq);
    rec.rows.push({
      sensor_uid:node.sensor_uid || "", sensor_id:node.sensor_uid || "", firmware_sensor_id:node.sensor_uid || "",
      node_id:node.nodeId, node_name:node.name || `Node ${node.nodeId}`,
      mount_site:node.mount_site || "other", body_part:node.body_part || "other", side:node.side || "unknown",
      subject_id:meta.subject, session_id:meta.session, trial_id:meta.trial, action_label:meta.action,
      phase_label:currentPhase, phase_annotation_source:"manual_button", phase_annotation_reviewed:false,
      phase_elapsed_ms:"", calibration_id:"", calibration_method_version:"",
      calibration_time_boot_ms:"", calibration_boot_id:"", calibrated:sample.calibrated,
      relative_angle_valid:false, relative_orientation_valid:false, unit_accel:"m/s^2", unit_gyro:"rad/s",
      host_time_iso:new Date(hostTimeMs).toISOString(), host_time_s:(hostTimeMs/1000).toFixed(6),
      host_elapsed_ms:Math.max(0,hostTimeMs-meta.started), boot_id:node.boot_id, boot_ms:sample.sample_ms, seq:sample.seq,
      stream_epoch:rec.streamEpoch, gap_before:gap,
      raw_accel_x:sample.raw_ax, raw_accel_y:sample.raw_ay, raw_accel_z:sample.raw_az,
      raw_gyro_x:sample.raw_gx, raw_gyro_y:sample.raw_gy, raw_gyro_z:sample.raw_gz,
      accel_x:sample.ax, accel_y:sample.ay, accel_z:sample.az,
      gyro_x:sample.gx, gyro_y:sample.gy, gyro_z:sample.gz,
      roll_deg:sample.angle_valid ? sample.roll_deg : "", pitch_deg:sample.angle_valid ? sample.pitch_deg : "",
      corrected_gyro_x:"", corrected_gyro_y:"", corrected_gyro_z:"", relative_roll_deg:"", relative_pitch_deg:"",
      relative_tilt_deg:"", yaw_deg:sample.angle_valid ? sample.yaw_deg : "", accel_norm_g:sample.accel_norm_g, gyro_norm_dps:sample.gyro_norm_dps,
      steady:"", valid:sample.valid, firmware_status:sample.status
    });
  }
  function downloadNodeCsv(nodeId, node, rows) {
    const csv = [CSV_COLUMNS.join(",")].concat(rows.map(row => CSV_COLUMNS.map(key => escCsv(row[key])).join(","))).join("\r\n");
    const url = URL.createObjectURL(new Blob(["\uFEFF" + csv], {type:"text/csv;charset=utf-8"}));
    downloadUrls.push(url);
    const link = document.createElement("a");
    const filePart = value => String(value || "").replace(/[^a-z0-9_-]+/gi,"_").replace(/^_+|_+$/g,"") || "data";
    link.href = url; link.download = `${filePart(window.datasetMetadata.trial)}_node${nodeId}_${filePart(node.mount_site || "other")}.csv`;
    link.textContent = `Node ${nodeId} · ${node.name || "IMU"} · ${rows.length.toLocaleString()} samples · ดาวน์โหลด CSV`;
    $("downloads").appendChild(link);
  }
  function stopRecording() {
    if (!recording) return;
    window.clearTimeout(guidedTimer);
    guidedTimer = 0;
    recording = false;
    setNodeConfigControlsDisabled(false);
    setCaptureControlsDisabled(false);
    $("recordStart").disabled = true;
    $("recordStop").disabled = true;
    const total = Object.values(recorders).reduce((sum,rec) => sum + rec.rows.length, 0);
    $("recordCount").textContent = `บันทึก ${total.toLocaleString()} samples; ทบทวน phase ก่อนสร้าง CSV`;
    if (!total) {
      $("recordStart").disabled = false;
      $("phaseEditor").replaceChildren();
      $("addPhaseBoundary").disabled = true;
      $("downloadCsv").disabled = true;
      setFeedback("ไม่มี sample ที่บันทึกได้", true);
      return;
    }
    if (!phaseEvents.length || phaseEvents[0].elapsedMs > 0) phaseEvents.unshift({elapsedMs:0,phase:"neutral",source:"recording_start",reviewed:false});
    renderPhaseReview();
    $("addPhaseBoundary").disabled = false;
    $("downloadCsv").disabled = false;
    setFeedback("ทบทวนเวลาและ phase ของแต่ละช่วง แล้วกดยืนยันเพื่อสร้าง CSV แยกตาม Node", false);
  }
  function renderPhaseReview() {
    const host = $("phaseEditor");
    host.replaceChildren();
    const heading = document.createElement("strong");
    heading.textContent = "ทบทวนจุดเปลี่ยน phase (เวลาเทียบจากเริ่มบันทึก)";
    host.appendChild(heading);
    phaseEvents.forEach((event,index) => {
      const row = document.createElement("div");
      row.className = "review-row";
      row.innerHTML = `<input type="number" min="0" step="0.05" aria-label="เวลาเป็นวินาที"><select aria-label="phase">${optionsHtml(Object.keys(PHASE_LABELS),PHASE_LABELS)}</select><label><input type="checkbox"> review แล้ว</label>`;
      row.querySelector("input[type=number]").value = (event.elapsedMs / 1000).toFixed(2);
      row.querySelector("select").value = event.phase;
      row.querySelector("input[type=checkbox]").checked = !!event.reviewed;
      row.dataset.index = String(index);
      host.appendChild(row);
    });
  }
  function addPhaseBoundary() {
    if (recording) return;
    phaseEvents = readPhaseReviewEdits();
    let latestSample = 0;
    Object.values(recorders).forEach(rec => rec.rows.forEach(row => {
      latestSample = Math.max(latestSample, Number(row.host_elapsed_ms) || 0);
    }));
    phaseEvents.push({elapsedMs:Math.round(latestSample / 2),phase:"neutral",source:"review",reviewed:false});
    renderPhaseReview();
  }
  function readPhaseReviewEdits() {
    const edits = [...$("phaseEditor").querySelectorAll(".review-row")];
    return edits.map(row => {
      const index = Number(row.dataset.index);
      const old = phaseEvents[index] || {};
      const phase = row.querySelector("select").value;
      return {
        elapsedMs:Math.max(0, Math.round(Number(row.querySelector("input[type=number]").value || 0) * 1000)),
        phase,
        source:phase !== old.phase ? "review" : (old.source || "guided"),
        reviewed:row.querySelector("input[type=checkbox]").checked
      };
    }).sort((a,b) => a.elapsedMs - b.elapsedMs);
  }
  function applyPhaseReviewAndDownload() {
    if (recording) return;
    phaseEvents = readPhaseReviewEdits();
    if (!phaseEvents.length || phaseEvents[0].elapsedMs > 0) phaseEvents.unshift({elapsedMs:0,phase:"neutral",source:"recording_start",reviewed:false});
    for (const rec of Object.values(recorders)) {
      for (const sample of rec.rows) {
        let boundary = phaseEvents[0];
        for (const event of phaseEvents) {
          if (event.elapsedMs > Number(sample.host_elapsed_ms)) break;
          boundary = event;
        }
        sample.phase_label = boundary.phase;
        sample.phase_annotation_source = boundary.source;
        sample.phase_annotation_reviewed = boundary.reviewed;
        sample.phase_elapsed_ms = Math.max(0, Number(sample.host_elapsed_ms) - boundary.elapsedMs);
      }
    }
    $("downloads").replaceChildren();
    let total = 0, files = 0;
    for (const key of Object.keys(recorders)) {
      const id = Number(key), rec = recorders[id], node = nodeCache[id];
      total += rec.rows.length;
      if (rec.rows.length) { downloadNodeCsv(id, node || {nodeId:id}, rec.rows); files++; }
    }
    $("recordStart").disabled = false;
    $("downloadCsv").disabled = true;
    $("addPhaseBoundary").disabled = true;
    $("recordCount").textContent = `${total.toLocaleString()} samples · ${files} CSV`;
    setFeedback(`สร้าง CSV แยก ${files} Node แล้ว; ตรวจ checkbox เพื่อระบุช่วงที่ผ่านการ review`, false);
    for (const url of downloadUrls.splice(0)) setTimeout(() => URL.revokeObjectURL(url), 60000);
  }
  async function pollSamples() {
    if (pollBusy) return;
    pollBusy = true;
    try {
      const url = new URL("/samples", location.href);
      for (let id=1; id<=4; id++) {
        const cursor = cursors[id];
        if (cursor && cursor.afterSeq !== null) { url.searchParams.set(`n${id}_boot_id`, String(cursor.bootId)); url.searchParams.set(`n${id}_after_seq`, String(cursor.afterSeq)); }
      }
      const response = await fetch(url, {cache:"no-store"});
      if (!response.ok) throw new Error(`Gateway /samples HTTP ${response.status}`);
      const payload = await response.json();
      const receivedAt = Date.now();
      for (const node of payload.nodes || []) {
        updateNodeCard(node);
        const id = Number(node.nodeId);
        let cursor = cursors[id];
        const newSession = !cursor || cursor.bootId !== Number(node.boot_id);
        if (newSession) cursor = cursors[id] = {bootId:Number(node.boot_id),afterSeq:null};
        const fresh = newSession || cursor.afterSeq === null
          ? (node.samples || [])
          : (node.samples || []).filter(sample => isSeqAfter(sample.seq,cursor.afterSeq));
        const newestMs = fresh.length ? Number(fresh[fresh.length-1].sample_ms) : 0;
        if (node.dropped && recording) {
          if (!recorders[id]) recorders[id] = {rows:[], startedAt:window.datasetMetadata.started, startSeq:null, startBootId:Number(node.boot_id), lastSeq:null, bootId:null, streamEpoch:0, gapPending:false};
          recorders[id].gapPending = true;
        }
        for (const sample of fresh) {
          const ageMs = (newestMs - Number(sample.sample_ms) + 4294967296) % 4294967296;
          const sampleHostMs = receivedAt - ageMs;
          captureSample(node,sample,sampleHostMs);
          cursor.afterSeq = Number(sample.seq);
        }
      }
      if (recording) {
        const count = Object.values(recorders).reduce((sum,rec) => sum+rec.rows.length,0);
        $("recordCount").textContent = `${count.toLocaleString()} samples · กำลังบันทึก`;
      }
      const elapsed = Date.now()-lastPollAt; lastPollAt=Date.now();
      if (elapsed > 800 && recording) setFeedback("การตอบสนองช้า; ตรวจ gap_before ใน CSV หลังบันทึก", true);
      $("gatewayStatus").textContent = "Gateway online";
      $("gatewayStatus").classList.add("online");
    } catch(error) {
      $("gatewayStatus").textContent = "Gateway offline";
      $("gatewayStatus").classList.remove("online");
      setFeedback(`อ่านข้อมูลไม่สำเร็จ: ${error.message}`, true);
    } finally { pollBusy=false; }
  }
  document.querySelectorAll(".phase").forEach(button => button.addEventListener("click", () => updatePhase(button.dataset.phase)));
  $("recordStart").addEventListener("click",startRecording);
  $("recordStop").addEventListener("click",stopRecording);
  $("addPhaseBoundary").addEventListener("click",addPhaseBoundary);
  $("downloadCsv").addEventListener("click",applyPhaseReviewAndDownload);
  $("sessionId").value = "session-" + new Date().toISOString().replace(/[-:.TZ]/g,"").slice(0,14);
  refreshNodeList().catch(error => setFeedback(`อ่านสถานะ Node ไม่สำเร็จ: ${error.message}`,true));
  setInterval(pollSamples,100);
  window.addEventListener("beforeunload",() => downloadUrls.forEach(URL.revokeObjectURL));
})();
</script>
</body>
</html>
)PLANKOHTML";
