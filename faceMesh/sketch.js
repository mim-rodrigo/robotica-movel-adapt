// === FaceMesh + MQTT: angulação (yaw/pitch/roll) com calibração (tara), FPS (EMA),
//     layout responsivo, renderização ESPELHADA, e desenho robusto do vídeo ===

// ---------- Config FaceMesh ----------
let faceMesh;
let options = { maxFaces: 1, refineLandmarks: false, flipped: false }; // NÃO invertimos a entrada do modelo

let video;
let faces = [];

// ---------- MQTT ----------
const mqttUrl      = "wss://0e51aa7bffcf45618c342e30a71338e8.s1.eu.hivemq.cloud:8884/mqtt";
const mqttTopic    = "facemesh/angle"; // publica YAW calibrado (graus)
const commandTopic = "facemesh/cmd";   // yaw para o robô + medição RTT
const pongTopic    = "facemesh/pong";  // respostas do ESP32 com nonce/timestamp

const connectOptions = {
  username: "hivemq.webclient.1761227941253",
  password: "&a9<Vzb3sC0A!6ZB>xTm",
  clientId: "p5js_client_" + parseInt(Math.random() * 1000)
};

let client;
let mqttInitialized = false;

// ---------- Medição de latência (RTT) ----------
const perf = (typeof performance !== 'undefined') ? performance : { now: () => Date.now() };
const pendingCommands = new Map();
const latencyStats = {
  lastRtt: null,
  min: null,
  max: null,
  avg: 0,
  count: 0,
  lastYaw: null,
  lastAction: null,
  lastStatus: null,
  lastT0: null,
  lastExecutedAt: null,
  lastReceivedAt: null
};

const COMMAND_DEBOUNCE_MS = 250;   // tempo mínimo entre mensagens sucessivas (quando variar)
const COMMAND_REPEAT_MS   = 2000;  // reenvia mesmo yaw periodicamente para medir RTT
const COMMAND_TIMEOUT_MS  = 5000;  // expira RTT caso não haja pong
const YAW_EPSILON_DEG     = 1.0;   // variação mínima para considerar mudança
let lastYawSent = null;
let lastYawSentAt = 0;

// ---------- Calibração (tara) ----------
let yawOffset = 0, pitchOffset = 0, rollOffset = 0;
let lastRaw = null;     // {yaw, pitch, roll} sem calibração
let lastCalibTS = null; // millis() da última calibração
let btnCalib;           // p5 DOM button

// ---------- FPS (EMA – suavizado) ----------
let fpsEMA = null;

// ---------- Retângulo de desenho do VÍDEO (responsivo) ----------
let drawRect = { x: 0, y: 0, w: 0, h: 0, sx: 1, sy: 1 }; // x,y,w,h e escalas sx,sy

// ---------- p5 lifecycle ----------
function preload() {
  faceMesh = ml5.faceMesh(options);
}

function setup() {
  // canvas ocupa a janela inteira
  createCanvas(windowWidth, windowHeight);

  // captura; FaceMesh usa o elemento de vídeo internamente
  video = createCapture(VIDEO, () => {
    // assim que a stream subir, calculamos o retângulo de desenho
    updateDrawRect();
  });
  video.hide();

  // botão de tara
  btnCalib = createButton('Zerar ângulos (tara)');
  btnCalib.mousePressed(calibrateAngles);

  // meta de FPS
  setFrameRate(60);
}

function windowResized() {
  resizeCanvas(windowWidth, windowHeight);
  updateDrawRect();
}

// ---------- util: dimensões reais do <video> ----------
function getVideoDims() {
  const vw = video?.elt?.videoWidth  || video?.width  || 640;
  const vh = video?.elt?.videoHeight || video?.height || 480;
  return { vw, vh };
}

// ---------- calcula área de desenho (contain/letterbox) ----------
function updateDrawRect() {
  const { vw, vh } = getVideoDims();
  const dstW = width;
  const dstH = height;

  const scale = Math.min(dstW / vw, dstH / vh);
  const w = vw * scale;
  const h = vh * scale;
  const x = (dstW - w) / 2;
  const y = (dstH - h) / 2;

  drawRect = { x, y, w, h, sx: w / vw, sy: h / vh };
}

function calibrateAngles() {
  if (lastRaw) {
    yawOffset   = lastRaw.yaw;
    pitchOffset = lastRaw.pitch;
    rollOffset  = lastRaw.roll;
    lastCalibTS = millis();
    console.log(`Calibrado: offsets = yaw ${yawOffset.toFixed(1)}°, pitch ${pitchOffset.toFixed(1)}°, roll ${rollOffset.toFixed(1)}°`);
  } else {
    console.warn('Não foi possível calibrar: nenhum rosto detectado ainda.');
  }
}

function gotFaces(results) {
  faces = results;
}

function draw() {
  background(0);

  reapExpiredCommands();

  // -------- desenha o VÍDEO (robusto + ESPELHADO) --------
  const { vw, vh } = getVideoDims();

  if (vw === 0 || vh === 0) {
    // ainda inicializando a câmera
    updateDrawRect();
    fill(255); noStroke(); textAlign(CENTER, CENTER);
    text('Inicializando câmera…', width/2, height/2);
  } else {
    // Recalcula se o vídeo mudou de tamanho ou ainda não definimos a área
    const needRecalc =
      drawRect.w === 0 || drawRect.h === 0 ||
      Math.abs(drawRect.sx - (drawRect.w / vw)) > 1e-6 ||
      Math.abs(drawRect.sy - (drawRect.h / vh)) > 1e-6;
    if (needRecalc) updateDrawRect();

    // Renderização espelhada via transformações (mais estável que largura negativa)
    push();
    translate(drawRect.x + drawRect.w, drawRect.y); // âncora topo-direito
    scale(-drawRect.w / vw, drawRect.h / vh);       // espelha no X e ajusta escala
    image(video, 0, 0, vw, vh);
    pop();
  }

  // -------- inicializa MQTT uma vez --------
  if (!mqttInitialized) {
    if (typeof mqtt !== 'undefined') {
      mqttInitialized = true;
      client = mqtt.connect(mqttUrl, connectOptions);

      client.on("connect", function() {
        console.log("MQTT Conectado!");
        client.subscribe(pongTopic, function(err) {
          if (err) {
            console.warn("Falha ao inscrever em", pongTopic, err);
          } else {
            console.log("Inscrito em", pongTopic);
          }
        });
        // inicia FaceMesh só após MQTT on-line (opcional; mantém sequenciamento)
        faceMesh.detectStart(video, gotFaces);
      });
      client.on("error", function(err) {
        console.warn("Falha ao conectar MQTT:", err);
        mqttInitialized = false; // para tentar de novo
      });
      client.on("reconnect", function() {
        console.log("MQTT Reconectando...");
      });
      client.on("message", function(topic, message) {
        if (topic === pongTopic) {
          handlePongMessage(message.toString());
        }
      });
    } else {
      fill(255, 0, 0);
      textAlign(CENTER);
      textSize(20);
      text("Aguardando biblioteca MQTT...", width / 2, height / 2);
    }
    // segue para HUD mesmo assim
  }

  // alvo no centro do CANVAS (referência visual)
  const cx = width / 2;
  const cy = height / 2;
  stroke(255, 0, 0); strokeWeight(1);
  line(cx - 10, cy, cx + 10, cy);
  line(cx, cy - 10, cx, cy + 10);
  noStroke();

  // -------- Face + Ângulos --------
  if (faces.length > 0) {
    const face = faces[0];

    const getKP = (idx) => {
      if (!face.keypoints || face.keypoints.length === 0) return null;
      if (idx >= 0 && idx < face.keypoints.length) return face.keypoints[idx];
      return null;
    };

    // Índices MediaPipe/TFJS comuns
    const L = getKP(33);           // canto olho esquerdo
    const R = getKP(263);          // canto olho direito
    let N = getKP(4) || getKP(1);  // ponta do nariz (fallback)

    if (L && R && N) {
      // Ângulos calculados em espaço ORIGINAL do modelo (não espelhado)
      const mid = {
        x: 0.5 * (L.x + R.x),
        y: 0.5 * (L.y + R.y),
        z: (("z" in L) && ("z" in R)) ? 0.5 * (L.z + R.z) : undefined
      };

      const u = vecNorm(vecSub(R, L)); // eixo olhos (esq->dir)
      const v = vecNorm(vecSub(N, mid));// olhos -> nariz

      let yawDeg = 0, pitchDeg = 0, rollDeg = 0;

      if (hasZ(L, R, N)) {
        const n = vecNorm(cross(u, v));   // normal aproximada do rosto
        // convenção: yaw (+) cabeça vira para a direita da câmera
        yawDeg   = rad2deg(Math.atan2(n.x, n.z));
        pitchDeg = rad2deg(Math.atan2(-n.y, n.z));
        rollDeg  = rad2deg(Math.atan2(u.y, u.x));
      } else {
        // fallback 2D aproximado
        const eyeDist = dist2D(L, R) + 1e-6;
        const dx = (N.x - mid.x) / eyeDist;
        const dy = (N.y - mid.y) / eyeDist;
        yawDeg   = clamp(dx * 60, -60, 60);
        pitchDeg = clamp(-dy * 60, -60, 60);
        rollDeg  = rad2deg(Math.atan2(R.y - L.y, R.x - L.x));
      }

      // guarda leitura crua e aplica offsets (tara)
      lastRaw = { yaw: yawDeg, pitch: pitchDeg, roll: rollDeg };
      const yawCal   = yawDeg   - yawOffset;
      const pitchCal = pitchDeg - pitchOffset;
      const rollCal  = rollDeg  - rollOffset;

      // publica YAW calibrado
      if (client && client.connected) {
        const payload = yawCal.toFixed(1);
        client.publish(mqttTopic, payload);

        if (!this._lastLog || millis() - this._lastLog > 250) {
          console.log(`MQTT [${mqttTopic}] yaw_cal=${payload}° (pitch_cal=${pitchCal.toFixed(1)}°, roll_cal=${rollCal.toFixed(1)}°)`);
          this._lastLog = millis();
        }
      }

      maybeSendYawCommand(yawCal);

      // ---- Desenho dos keypoints/linhas ESPELHADOS no CANVAS ----
      const mapPtMirror = (p) => ({
        x: drawRect.x + (drawRect.w - p.x * drawRect.sx),
        y: drawRect.y + (p.y * drawRect.sy)
      });

      const Lc = mapPtMirror(L);
      const Rc = mapPtMirror(R);
      const Nc = mapPtMirror(N);
      const midc = mapPtMirror(mid);

      // keypoints
      for (let j = 0; j < face.keypoints.length; j++) {
        const kp = face.keypoints[j];
        const kc = mapPtMirror(kp);
        fill(0, 255, 0); noStroke(); circle(kc.x, kc.y, 3);
      }
      // linhas de referência
      stroke(255, 255, 0); strokeWeight(2);
      line(Lc.x, Lc.y, Rc.x, Rc.y);     // linha dos olhos
      line(midc.x, midc.y, Nc.x, Nc.y); // olhos -> nariz
      noStroke(); fill(255, 255, 0); circle(Nc.x, Nc.y, 7);

      // HUD ângulos (calibrados)
      fill(255); noStroke();
      rect(6, height - 86, 540, 78, 6);
      fill(0); textSize(14);
      text(`Yaw (cal):   ${yawCal.toFixed(1)}°   |   Pitch (cal): ${pitchCal.toFixed(1)}°   |   Roll (cal): ${rollCal.toFixed(1)}°`, 12, height - 62);
      if (lastCalibTS !== null) {
        text(`Tara ativa • offsets = [${yawOffset.toFixed(1)}°, ${pitchOffset.toFixed(1)}°, ${rollOffset.toFixed(1)}°]`, 12, height - 42);
        text(`Calibrado há ${( (millis()-lastCalibTS)/1000 ).toFixed(1)} s`, 12, height - 24);
      } else {
        text(`Sem tara • clique em "Zerar ângulos (tara)"`, 12, height - 42);
      }

      // linha do centro do CANVAS até o nariz (já no espaço espelhado)
      stroke(255, 255, 0); strokeWeight(1.5);
      line(width/2, height/2, Nc.x, Nc.y);
      noStroke();

    } else {
      drawNoFace();
    }
  } else {
    drawNoFace();
  }

  // -------- FPS: EMA --------
  const fpsInstant = frameRate();
  if (fpsEMA === null) fpsEMA = fpsInstant;
  fpsEMA = 0.9 * fpsEMA + 0.1 * fpsInstant;

  fill(255);
  noStroke();
  rect(width - 140, 10, 130, 26, 6);
  fill(0);
  textSize(14);
  text(`FPS: ${fpsEMA.toFixed(1)}`, width - 130, 28);

  drawLatencyHUD();
}

// ---------- Envio de yaw + RTT ----------
function maybeSendYawCommand(yawCal) {
  if (!client || !client.connected) return;

  const now = millis();
  const changed = (lastYawSent === null) || Math.abs(yawCal - lastYawSent) >= YAW_EPSILON_DEG;
  if (!changed && (now - lastYawSentAt) < COMMAND_REPEAT_MS) return;
  if (changed && (now - lastYawSentAt) < COMMAND_DEBOUNCE_MS) return;

  sendYawCommand(yawCal);
}

function sendYawCommand(yawDeg) {
  if (!client || !client.connected) return;

  const nonce = generateNonce();
  const t0 = Date.now();
  const yawStr = yawDeg.toFixed(1);
  const payload = `${yawStr}|${nonce}|${t0}`;

  client.publish(commandTopic, payload);
  pendingCommands.set(nonce, {
    yaw: yawDeg,
    t0,
    perfStart: perf.now()
  });

  lastYawSent = yawDeg;
  lastYawSentAt = millis();
  latencyStats.lastYaw = yawDeg;
  latencyStats.lastAction = 'aguardando';
  latencyStats.lastStatus = 'pendente';

  console.log(`MQTT [${commandTopic}] yaw=${yawStr} nonce=${nonce} t0=${t0}`);
}

function handlePongMessage(rawMessage) {
  const parts = rawMessage.split('|');
  if (parts.length < 5) {
    console.warn('Pong inválido:', rawMessage);
    return;
  }

  const [nonce, t0Str, execTsStr, yawEcho = '', actionEcho = '', status = 'ok'] = parts;
  const pending = pendingCommands.get(nonce);
  if (!pending) {
    console.warn('Pong sem pendência correspondente:', rawMessage);
    return;
  }

  pendingCommands.delete(nonce);

  const nowPerf = perf.now();
  const rtt = nowPerf - pending.perfStart;
  const prevCount = latencyStats.count;
  const statusText = status || 'ok';
  const yawEchoNum = parseFloat(yawEcho);

  latencyStats.count = prevCount + 1;
  latencyStats.lastRtt = rtt;
  latencyStats.min = prevCount === 0 ? rtt : Math.min(latencyStats.min, rtt);
  latencyStats.max = prevCount === 0 ? rtt : Math.max(latencyStats.max, rtt);
  latencyStats.avg = prevCount === 0 ? rtt : ((latencyStats.avg * prevCount) + rtt) / (prevCount + 1);
  latencyStats.lastYaw = Number.isFinite(yawEchoNum) ? yawEchoNum : pending.yaw;
  latencyStats.lastAction = actionEcho || '—';
  latencyStats.lastStatus = statusText;
  latencyStats.lastT0 = t0Str;
  latencyStats.lastExecutedAt = execTsStr || null;
  latencyStats.lastReceivedAt = Date.now();

  console.log(`PONG nonce=${nonce} yaw=${latencyStats.lastYaw?.toFixed?.(1) ?? yawEcho} action=${latencyStats.lastAction} status=${statusText} RTT=${rtt.toFixed(1)} ms`);
}

function reapExpiredCommands() {
  if (pendingCommands.size === 0) return;

  const nowPerf = perf.now();
  for (const [nonce, entry] of pendingCommands) {
    if (nowPerf - entry.perfStart > COMMAND_TIMEOUT_MS) {
      const yawTxt = (typeof entry.yaw === 'number') ? entry.yaw.toFixed(1) : entry.yaw;
      console.warn(`Timeout aguardando pong para nonce=${nonce} (yaw=${yawTxt})`);
      pendingCommands.delete(nonce);
      latencyStats.lastYaw = entry.yaw;
      latencyStats.lastAction = 'timeout';
      latencyStats.lastStatus = 'timeout';
      latencyStats.lastRtt = null;
    }
  }
}

function drawLatencyHUD() {
  const panelWidth = 320;
  const panelHeight = 96;
  const panelX = width - panelWidth - 10;
  const panelY = 46;

  fill(255);
  noStroke();
  rect(panelX, panelY, panelWidth, panelHeight, 6);

  fill(0);
  textSize(14);

  const lastRttTxt = (latencyStats.lastRtt !== null)
    ? `${latencyStats.lastRtt.toFixed(1)} ms`
    : '—';
  const minTxt = latencyStats.count > 0 ? `${latencyStats.min.toFixed(1)} ms` : '—';
  const maxTxt = latencyStats.count > 0 ? `${latencyStats.max.toFixed(1)} ms` : '—';
  const avgTxt = latencyStats.count > 0 ? `${latencyStats.avg.toFixed(1)} ms` : '—';
  const statusTxt = latencyStats.lastStatus || '—';
  const yawTxt = (latencyStats.lastYaw !== null && latencyStats.lastYaw !== undefined)
    ? `${latencyStats.lastYaw.toFixed(1)}°`
    : '—';
  const actionTxt = latencyStats.lastAction || '—';
  const pendingCount = pendingCommands.size;

  text(`RTT último: ${lastRttTxt}`, panelX + 10, panelY + 24);
  text(`Mín/Máx/Média: ${minTxt} / ${maxTxt} / ${avgTxt}`, panelX + 10, panelY + 44);
  text(`Yaw echo: ${yawTxt} • Ação: ${actionTxt} (${statusTxt})`, panelX + 10, panelY + 64);
  text(`Amostras: ${latencyStats.count} • Pendentes: ${pendingCount}`, panelX + 10, panelY + 84);
}

function generateNonce() {
  return Math.random().toString(36).slice(2, 10) + Math.floor(perf.now()).toString(36);
}

// ---------- Helpers geométricos ----------
function vecSub(a, b) {
  return { x: a.x - b.x, y: a.y - b.y, z: (("z" in a && "z" in b) ? (a.z - b.z) : undefined) };
}
function vecLen(a) {
  const z = ("z" in a && a.z !== undefined) ? a.z : 0;
  return Math.hypot(a.x, a.y, z);
}
function vecNorm(a) {
  const L = vecLen(a) + 1e-9;
  const z = ("z" in a && a.z !== undefined) ? a.z : 0;
  return { x: a.x / L, y: a.y / L, z: z / L };
}
function cross(a, b) {
  const az = ("z" in a && a.z !== undefined) ? a.z : 0;
  const bz = ("z" in b && b.z !== undefined) ? b.z : 0;
  return {
    x: a.y * bz - az * b.y,
    y: az * b.x - a.x * bz,
    z: a.x * b.y - a.y * b.x
  };
}
function rad2deg(r) { return r * 180 / Math.PI; }
function dist2D(a, b) { return Math.hypot(a.x - b.x, a.y - b.y); }
function clamp(v, lo, hi) { return Math.max(lo, Math.min(hi, v)); }
function hasZ(...pts) {
  return pts.every(p => ("z" in p) && p.z !== undefined && !Number.isNaN(p.z));
}

// ---------- HUD ausência de rosto ----------
function drawNoFace() {
  fill(255); noStroke();
  rect(6, height - 26, 260, 20, 6);
  fill(0); textSize(14);
  text("Sem rosto detectado…", 12, height - 12);
}