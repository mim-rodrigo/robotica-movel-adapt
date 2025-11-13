/**
 * Descrição:
 * FaceMesh + MQTT para cálculo de ângulos da face (Yaw, Pitch, Roll) e envio para robô.
 * Utiliza o método robusto de 4 pontos (bochechas, ponte nasal, queixo) para
 * desacoplamento do Yaw/Pitch.
 * Inclui calibração (tara), medição de latência (RTT) e renderização espelhada.
 *
 * Versão atual: 2.1 (10/11/2025)
 * - Corrige a ordem do produto vetorial (cross) na linha 73.
 * - Inverte de cross(v_vec, u_vec) para cross(u_vec, v_vec).
 * - Isso garante que o vetor normal 'n' aponte para fora do rosto,
 *   centralizando o 'yaw' em 0° quando o usuário olha para frente.
 *
 * Versão anterior: 2.0 (10/11/2025)
 * - Lógica de ângulos alterada para método robusto de 4 pontos (pontos 234, 454, 6, 152).
 * - Isso corrigiu o acoplamento onde o 'pitch' (levantar/abaixar cabeça) afetava o 'yaw'.
 */

// ---------- Config FaceMesh ----------
let faceMesh;
let options = { maxFaces: 1, refineLandmarks: false, flipped: false }; // NÃO invertimos a entrada do modelo

let video;
let faces = [];

// ---------- MQTT ----------
const mqttUrl      = "wss://0e51aa7bffcf45618c342e30a71338e8.s1.eu.hivemq.cloud:8884/mqtt";
const mqttTopic    = "facemesh/angle"; // publica YAW calibrado (graus)
const commandTopic = "facemesh/cmd";
// yaw para o robô + medição RTT
const pongTopic    = "facemesh/pong";
// respostas do ESP32 com nonce/timestamp

const connectOptions = {
  username: "hivemq.webclient.1761227941253",
  password: "&a9<Vzb3sC0A!6ZB>xTm",
  clientId: "p5js_client_" + parseInt(Math.random() * 1000)
};
const yawDeadbandLeftDeg  = 15.0;
const yawDeadbandRightDeg = 15.0;

const ANGLE_EMA_ALPHA = 0.2;
// suavização das leituras finais (0<alpha<=1)

const pitchForwardThresholdDeg = -10;
const pitchReverseThresholdDeg = 10;

const filteredAngles = {
  yaw: null,
  pitch: null,
  roll: null
};

let client;
let mqttInitialized = false;

// ---------- Medição de latência (RTT) ----------
const perf = (typeof performance !== 'undefined') ?
performance : { now: () => Date.now() };
const pendingCommands = new Map();
const latencyStats = {
  lastRtt: null,
  min: null,
  max: null,
  avg: 0,
  count: 0,
  lastYaw: null,
  lastPitch: null,
  lastAction: null,
  lastStatus: null,
  lastT0: null,
  lastExecutedAt: null,
  lastReceivedAt: null
};
const COMMAND_DEBOUNCE_MS = 250;   // tempo mínimo entre mensagens sucessivas (quando variar)
const COMMAND_REPEAT_MS   = 2000;
// reenvia mesmo comando periodicamente para medir RTT
const COMMAND_TIMEOUT_MS  = 5000;
// expira RTT caso não haja pong
const commandEpsilon = {
  yaw: 1.0,
  pitch: 1.0
};

const lastAnglesSent = {
  yaw: null,
  pitch: null
};
let lastCommandSentAt = 0;
// ---------- Calibração (tara) ----------
let yawOffset = 0, pitchOffset = 0, rollOffset = 0;
let lastRaw = null;
// {yaw, pitch, roll} sem calibração
let lastCalibTS = null; // millis() da última calibração
let btnCalib;           // p5 DOM button
let btnToggleStream;
// botão para pausar/retomar envio MQTT
let streamingEnabled = true;

// ---------- FPS (EMA – suavizado) ----------
let fpsEMA = null;
// ---------- Retângulo de desenho do VÍDEO (responsivo) ----------
let drawRect = { x: 0, y: 0, w: 0, h: 0, sx: 1, sy: 1 };
// x,y,w,h e escalas sx,sy

// ---------- p5 lifecycle ----------
function preload() {
  faceMesh = ml5.faceMesh(options);
}

function setup() {
  // canvas ocupa a janela inteira
  createCanvas(windowWidth, windowHeight);

  // captura;
  // FaceMesh usa o elemento de vídeo internamente
  video = createCapture(VIDEO, () => {
    // assim que a stream subir, calculamos o retângulo de desenho
    updateDrawRect();
  });
  video.hide();

  // botão de tara
  btnCalib = createButton('Zerar ângulos (tara)');
  btnCalib.mousePressed(calibrateAngles);
  btnCalib.position(20, 20);

  btnToggleStream = createButton('Pausar envio MQTT');
  btnToggleStream.mousePressed(toggleStreaming);
  btnToggleStream.position(20, 60);
  updateStreamButtonLabel();

  // meta de FPS
  setFrameRate(60);
}

function windowResized() {
  resizeCanvas(windowWidth, windowHeight);
  updateDrawRect();
}

// ---------- util: dimensões reais do <video> ----------
function getVideoDims() {
  const vw = video?.elt?.videoWidth  || video?.width  ||
  640;
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
    resetAngleFilters();
    console.log(`Calibrado: offsets = yaw ${yawOffset.toFixed(1)}°, pitch ${pitchOffset.toFixed(1)}°, roll ${rollOffset.toFixed(1)}°`);
  } else {
    console.warn('Não foi possível calibrar: nenhum rosto detectado ainda.');
  }
}

function updateStreamButtonLabel() {
  if (!btnToggleStream) return;
  btnToggleStream.html(streamingEnabled ? 'Pausar envio MQTT' : 'Retomar envio MQTT');
}

function toggleStreaming() {
  streamingEnabled = !streamingEnabled;
  resetAngleFilters();
  updateStreamButtonLabel();

  if (!streamingEnabled) {
    console.log('Envio MQTT pausado – enviando comando de parada.');
    sendMotionCommand(NaN, NaN, { force: true });
    pendingCommands.clear();
    lastAnglesSent.yaw = null;
    lastAnglesSent.pitch = null;
    lastCommandSentAt = 0;
    latencyStats.lastYaw = null;
    latencyStats.lastPitch = null;
    latencyStats.lastAction = 'pausado';
    latencyStats.lastStatus = 'pausado';
    latencyStats.lastRtt = null;
  } else {
    console.log('Envio MQTT retomado.');
    lastAnglesSent.yaw = null;
    lastAnglesSent.pitch = null;
    lastCommandSentAt = 0;
    latencyStats.lastYaw = null;
    latencyStats.lastPitch = null;
    latencyStats.lastAction = '—';
    latencyStats.lastStatus = 'ativo';
    latencyStats.lastRtt = null;
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
      drawRect.w === 0 ||
      drawRect.h === 0 ||
      Math.abs(drawRect.sx - (drawRect.w / vw)) > 1e-6 ||
      Math.abs(drawRect.sy - (drawRect.h / vh)) > 1e-6;
    if (needRecalc) updateDrawRect();

    // Renderização espelhada via transformações (mais estável que largura negativa)
    push();
    translate(drawRect.x + drawRect.w, drawRect.y); // âncora topo-direito
    scale(-drawRect.w / vw, drawRect.h / vh);
    // espelha no X e ajusta escala
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

    // --- INÍCIO DA MODIFICAÇÃO V2.0 ---
    // Pontos robustos para eixos desacoplados
    const P_LEFT_CHEEK  = getKP(234); // Bochecha Esquerda
    const P_RIGHT_CHEEK = getKP(454); // Bochecha Direita
    const P_NOSE_BRIDGE = getKP(6);   // Ponte Nasal (entre olhos)
    const P_CHIN        = getKP(152); // Ponta do Queixo

    // Verificar se temos os 4 pontos E se eles têm dados 3D
    if (P_LEFT_CHEEK && P_RIGHT_CHEEK && P_NOSE_BRIDGE && P_CHIN && hasZ(P_LEFT_CHEEK, P_RIGHT_CHEEK, P_NOSE_BRIDGE, P_CHIN)) {

      // Calcular eixos (ainda não normalizados)
      // Eixo horizontal (u): da bochecha esquerda para a direita
      const u_vec = vecSub(P_RIGHT_CHEEK, P_LEFT_CHEEK);
      
      // Eixo vertical (v): da ponte nasal para o queixo
      const v_vec = vecSub(P_CHIN, P_NOSE_BRIDGE);

      // --- INÍCIO DA MODIFICAÇÃO V2.1 ---
      // Calcular a Normal (n) - o vetor "para frente"
      // A ordem (u, v) garante que o vetor 'n' aponte PARA FORA do rosto (+Z).
      const n_vec = cross(u_vec, v_vec);
      // --- FIM DA MODIFICAÇÃO V2.1 ---

      // Normalizar os 3 vetores de eixo
      const u = vecNorm(u_vec); // Eixo X do rosto (esquerda->direita)
      const v = vecNorm(v_vec); // Eixo Y do rosto (cima->baixo)
      const n = vecNorm(n_vec); // Eixo Z do rosto (frente)

      // Calcular os ângulos (Euler) a partir dos vetores de eixo
      // Convenção: yaw (+) cabeça vira para a direita da câmera
      let yawDeg   = rad2deg(Math.atan2(n.x, n.z));
      let pitchDeg = rad2deg(Math.atan2(-n.y, n.z));
      let rollDeg  = rad2deg(Math.atan2(u.y, u.x));
      // --- FIM DA MODIFICAÇÃO V2.0 (LÓGICA) ---

      // guarda leitura crua e aplica offsets (tara)
      lastRaw = { yaw: yawDeg, pitch: pitchDeg, roll: rollDeg };
      const yawCal   = yawDeg   - yawOffset;
      const pitchCal = pitchDeg - pitchOffset;
      const rollCal  = rollDeg  - rollOffset;

      const yawSmooth   = updateAngleFilter('yaw', yawCal);
      const pitchSmooth = updateAngleFilter('pitch', pitchCal);
      const rollSmooth  = updateAngleFilter('roll', rollCal);

      const yawOutput   = yawSmooth ?? yawCal;
      const pitchOutput = pitchSmooth ?? pitchCal;
      const rollOutput  = rollSmooth ?? rollCal;

      // publica YAW calibrado
      if (client && client.connected && streamingEnabled) {
        const payload = yawOutput.toFixed(1);
        client.publish(mqttTopic, payload);

        if (!this._lastLog || millis() - this._lastLog > 250) {
          console.log(`MQTT [${mqttTopic}] yaw_ema=${payload}° (yaw_cal=${yawCal.toFixed(1)}° | pitch_ema=${pitchOutput.toFixed(1)}° | roll_ema=${rollOutput.toFixed(1)}°)`);
          this._lastLog = millis();
        }
      }

      maybeSendMotionCommand(yawOutput, pitchOutput);
      
      // ---- Desenho dos keypoints/linhas ESPELHADOS no CANVAS ----
      const mapPtMirror = (p) => ({
        x: drawRect.x + (drawRect.w - p.x * drawRect.sx),
        y: drawRect.y + (p.y * drawRect.sy)
      });

      // keypoints (desenha todos)
      for (let j = 0; j < face.keypoints.length; j++) {
        const kp = face.keypoints[j];
        const kc = mapPtMirror(kp);
        fill(0, 255, 0); noStroke(); circle(kc.x, kc.y, 3);
      }
      
      // --- INÍCIO MODIFICAÇÃO DESENHO V2.0 ---
      // Mapeia os 4 pontos de referência para o canvas espelhado
      const P_LEFT_c  = mapPtMirror(P_LEFT_CHEEK);
      const P_RIGHT_c = mapPtMirror(P_RIGHT_CHEEK);
      const P_NOSE_c  = mapPtMirror(P_NOSE_BRIDGE);
      const P_CHIN_c  = mapPtMirror(P_CHIN);

      // linhas de referência (eixos)
      stroke(255, 255, 0); strokeWeight(2);
      line(P_LEFT_c.x, P_LEFT_c.y, P_RIGHT_c.x, P_RIGHT_c.y); // Eixo U (horizontal)
      line(P_NOSE_c.x, P_NOSE_c.y, P_CHIN_c.x, P_CHIN_c.y);  // Eixo V (vertical)
      noStroke();
      // --- FIM MODIFICAÇÃO DESENHO V2.0 ---

      // HUD ângulos (calibrados)
      fill(255); noStroke();
      rect(6, height - 126, 560, 118, 6);
      fill(0); textSize(14);
      const line1 = height - 100;
      const line2 = height - 80;
      const line3 = height - 60;
      const line4 = height - 40;
      text(`Yaw (cal/EMA): ${yawCal.toFixed(1)}° / ${yawOutput.toFixed(1)}°   |   Pitch: ${pitchOutput.toFixed(1)}°   |   Roll: ${rollOutput.toFixed(1)}°`, 12, line1);
      text(`Deadband Esq: ${yawDeadbandLeftDeg.toFixed(1)}°   |   Deadband Dir: ${yawDeadbandRightDeg.toFixed(1)}°   |   Pitch F/R: ${pitchForwardThresholdDeg.toFixed(0)}° / +${pitchReverseThresholdDeg.toFixed(0)}°`, 12, line2);
      const previewAction = determineMotionAction(yawOutput, pitchOutput);
      const actionLabelMap = {
        left: 'Girar Esquerda',
        right: 'Girar Direita',
        forward: 'Frente',
        reverse: 'Ré',
        stop: 'Parar'
      };
      const actionLabel = actionLabelMap[previewAction] || '—';
      text(`Streaming MQTT: ${streamingEnabled ? 'Ativo' : 'Pausado'} • Ação prevista: ${actionLabel}`, 12, line3);
      if (lastCalibTS !== null) {
        const elapsed = ((millis() - lastCalibTS) / 1000).toFixed(1);
        text(`Tara ativa • offsets = [${yawOffset.toFixed(1)}°, ${pitchOffset.toFixed(1)}°, ${rollOffset.toFixed(1)}°] • Calibrado há ${elapsed} s`, 12, line4);
      } else {
        text(`Sem tara • clique em "Zerar ângulos (tara)"`, 12, line4);
      }

      // linha do centro do CANVAS até a referência (ponte nasal)
      stroke(255, 255, 0);
      strokeWeight(1.5);
      line(width/2, height/2, P_NOSE_c.x, P_NOSE_c.y);
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

// ---------- Envio de yaw/pitch + RTT ----------
function determineYawAction(yawDeg) {
  if (!Number.isFinite(yawDeg)) return 'stop';
  if (yawDeg <= -yawDeadbandLeftDeg) return 'left';
  if (yawDeg >= yawDeadbandRightDeg) return 'right';
  return 'stop';
}

function determinePitchAction(pitchDeg) {
  if (!Number.isFinite(pitchDeg)) return 'stop';
  if (pitchDeg <= pitchForwardThresholdDeg) return 'forward';
  if (pitchDeg >= pitchReverseThresholdDeg) return 'reverse';
  return 'stop';
}

function determineMotionAction(yawDeg, pitchDeg) {
  const pitchAction = determinePitchAction(pitchDeg);
  if (pitchAction !== 'stop') return pitchAction;
  return determineYawAction(yawDeg);
}

function maybeSendMotionCommand(yawDeg, pitchDeg) {
  if (!client || !client.connected || !streamingEnabled) return;

  const now = millis();
  const yawChanged = Number.isFinite(yawDeg)
    ? ((lastAnglesSent.yaw === null) || Math.abs(yawDeg - lastAnglesSent.yaw) >= commandEpsilon.yaw)
    : (lastAnglesSent.yaw !== null);
  const pitchChanged = Number.isFinite(pitchDeg)
    ? ((lastAnglesSent.pitch === null) || Math.abs(pitchDeg - lastAnglesSent.pitch) >= commandEpsilon.pitch)
    : (lastAnglesSent.pitch !== null);
  const changed = yawChanged || pitchChanged;

  if (!changed && (now - lastCommandSentAt) < COMMAND_REPEAT_MS) return;
  if (changed && (now - lastCommandSentAt) < COMMAND_DEBOUNCE_MS) return;

  sendMotionCommand(yawDeg, pitchDeg);
}

function sendMotionCommand(yawDeg, pitchDeg, { force = false } = {}) {
  if (!client || !client.connected) return;
  if (!force && !streamingEnabled) return;

  const nonce = generateNonce();
  const t0 = Date.now();
  const yawStr = Number.isFinite(yawDeg) ?
    yawDeg.toFixed(1) : 'NaN';
  const pitchStr = Number.isFinite(pitchDeg) ?
    pitchDeg.toFixed(1) : 'NaN';
  const payload = `${yawStr}|${pitchStr}|${nonce}|${t0}`;

  client.publish(commandTopic, payload);
  pendingCommands.set(nonce, {
    yaw: Number.isFinite(yawDeg) ? yawDeg : null,
    pitch: Number.isFinite(pitchDeg) ? pitchDeg : null,
    t0,
    perfStart: perf.now()
  });
  lastAnglesSent.yaw = Number.isFinite(yawDeg) ? yawDeg : null;
  lastAnglesSent.pitch = Number.isFinite(pitchDeg) ? pitchDeg : null;
  lastCommandSentAt = millis();
  const previewAction = determineMotionAction(yawDeg, pitchDeg);
  latencyStats.lastYaw = Number.isFinite(yawDeg) ? yawDeg : null;
  latencyStats.lastPitch = Number.isFinite(pitchDeg) ? pitchDeg : null;
  latencyStats.lastAction = previewAction;
  latencyStats.lastStatus = force ? 'forçado' : 'pendente';

  console.log(`MQTT [${commandTopic}] yaw=${yawStr} pitch=${pitchStr} nonce=${nonce} t0=${t0}`);
}

function handlePongMessage(rawMessage) {
  const parts = rawMessage.split('|');
  if (parts.length < 7) {
    console.warn('Pong inválido:', rawMessage);
    return;
  }

  const [nonce, t0Str, execTsStr, yawEcho = '', pitchEcho = '', actionEcho = '', status = 'ok'] = parts;
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
  const pitchEchoNum = parseFloat(pitchEcho);

  latencyStats.count = prevCount + 1;
  latencyStats.lastRtt = rtt;
  latencyStats.min = prevCount === 0 ?
    rtt : Math.min(latencyStats.min, rtt);
  latencyStats.max = prevCount === 0 ? rtt : Math.max(latencyStats.max, rtt);
  latencyStats.avg = prevCount === 0 ?
    rtt : ((latencyStats.avg * prevCount) + rtt) / (prevCount + 1);
  latencyStats.lastYaw = Number.isFinite(yawEchoNum) ? yawEchoNum : pending.yaw;
  latencyStats.lastPitch = Number.isFinite(pitchEchoNum) ? pitchEchoNum : pending.pitch;
  latencyStats.lastAction = actionEcho || '—';
  latencyStats.lastStatus = statusText;
  latencyStats.lastT0 = t0Str;
  latencyStats.lastExecutedAt = execTsStr || null;
  latencyStats.lastReceivedAt = Date.now();
  console.log(`PONG nonce=${nonce} yaw=${latencyStats.lastYaw?.toFixed?.(1) ?? yawEcho} pitch=${latencyStats.lastPitch?.toFixed?.(1) ?? pitchEcho} action=${latencyStats.lastAction} status=${statusText} RTT=${rtt.toFixed(1)} ms`);
}

function reapExpiredCommands() {
  if (pendingCommands.size === 0) return;
  const nowPerf = perf.now();
  for (const [nonce, entry] of pendingCommands) {
    if (nowPerf - entry.perfStart > COMMAND_TIMEOUT_MS) {
      const yawTxt = (typeof entry.yaw === 'number') ?
      entry.yaw.toFixed(1) : entry.yaw;
      const pitchTxt = (typeof entry.pitch === 'number') ? entry.pitch.toFixed(1) : entry.pitch;
      console.warn(`Timeout aguardando pong para nonce=${nonce} (yaw=${yawTxt} | pitch=${pitchTxt ?? '—'})`);
      pendingCommands.delete(nonce);
      latencyStats.lastYaw = entry.yaw;
      latencyStats.lastPitch = entry.pitch ?? null;
      latencyStats.lastAction = 'timeout';
      latencyStats.lastStatus = 'timeout';
      latencyStats.lastRtt = null;
    }
  }
}

function drawLatencyHUD() {
  const panelWidth = 340;
  const panelHeight = 124;
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
  const maxTxt = latencyStats.count > 0 ?
  `${latencyStats.max.toFixed(1)} ms` : '—';
  const avgTxt = latencyStats.count > 0 ? `${latencyStats.avg.toFixed(1)} ms` : '—';
  const statusTxt = latencyStats.lastStatus ||
  '—';
  const yawTxt = (latencyStats.lastYaw !== null && latencyStats.lastYaw !== undefined)
    ? `${latencyStats.lastYaw.toFixed(1)}°`
    : '—';
  const pitchTxt = (latencyStats.lastPitch !== null && latencyStats.lastPitch !== undefined)
    ? `${latencyStats.lastPitch.toFixed(1)}°`
    : '—';
  const actionTxt = latencyStats.lastAction || '—';
  const pendingCount = pendingCommands.size;
  text(`RTT último: ${lastRttTxt}`, panelX + 10, panelY + 24);
  text(`Mín/Máx/Média: ${minTxt} / ${maxTxt} / ${avgTxt}`, panelX + 10, panelY + 44);
  text(`Yaw echo: ${yawTxt} • Pitch echo: ${pitchTxt}`, panelX + 10, panelY + 64);
  text(`Ação: ${actionTxt} (${statusTxt})`, panelX + 10, panelY + 84);
  text(`Amostras: ${latencyStats.count} • Pendentes: ${pendingCount}`, panelX + 10, panelY + 104);
}

function generateNonce() {
  return Math.random().toString(36).slice(2, 10) + Math.floor(perf.now()).toString(36);
}

function resetAngleFilters() {
  filteredAngles.yaw = null;
  filteredAngles.pitch = null;
  filteredAngles.roll = null;
}

function updateAngleFilter(axis, value) {
  if (!Number.isFinite(value)) {
    return filteredAngles[axis];
  }

  const previous = filteredAngles[axis];
  const filtered = previous === null
    ?
    value
    : (ANGLE_EMA_ALPHA * value) + ((1 - ANGLE_EMA_ALPHA) * previous);

  filteredAngles[axis] = filtered;
  return filtered;
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
function dist2D(a, b) { return Math.hypot(a.x - b.x, a.y - b.y);
}
function clamp(v, lo, hi) { return Math.max(lo, Math.min(hi, v)); }
function hasZ(...pts) {
  return pts.every(p => ("z" in p) && p.z !== undefined && !Number.isNaN(p.z));
}

// ---------- HUD ausência de rosto ----------
function drawNoFace() {
  fill(255); noStroke();
  rect(6, height - 26, 260, 20, 6);
  fill(0);
  textSize(14);
  text("Sem rosto detectado…", 12, height - 12);
}
