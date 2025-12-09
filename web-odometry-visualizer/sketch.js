// Dimensões reais do robô (cm)
const ROBOT_DIM = {
  length: 75,
  width: 45,
  rearTrack: 62,
  rearWheelRadius: 12.5,
  rearWheelWidth: 10,
  frontWheelRadius: 10,
  frontWheelWidth: 10,
};

const SCALE = 80; // pixels por metro
const TRAIL_LIMIT = 8000;

let pose = { x: 0, y: 0, phi: 0 };
let lastPose = null;
let trail = [];
let totalDistance = 0;

let client = null;
let statusLabel;
let xLabel;
let yLabel;
let headingLabel;
let distanceLabel;
let connectBtn;
let resetBtn;

const defaults = {
  host: '0e51aa7bffcf45618c342e30a71338e8.s1.eu.hivemq.cloud',
  port: 8884,
  username: 'hivemq.webclient.1761227941253',
  password: '&a9<Vzb3sC0A!6ZB>xTm',
  odomTopic: 'robot/odometry',
};

function setup() {
  const canvas = createCanvas(960, 640);
  canvas.parent('p5-container');

  statusLabel = select('#status');
  xLabel = select('#poseX');
  yLabel = select('#poseY');
  headingLabel = select('#poseHeading');
  distanceLabel = select('#distance');
  connectBtn = select('#connectBtn');
  resetBtn = select('#resetBtn');

  select('#host').value(defaults.host);
  select('#port').value(defaults.port);
  select('#username').value(defaults.username);
  select('#password').value(defaults.password);
  select('#odomTopic').value(defaults.odomTopic);

  connectBtn.mousePressed(connectToBroker);
  resetBtn.mousePressed(resetPath);

  frameRate(60);
}

function draw() {
  background('#0b1021');
  drawGrid();

  translate(width / 2, height / 2);
  scale(1, -1);

  drawTrail();
  drawRobot();
}

function drawGrid() {
  push();
  noFill();
  stroke('#131c33');
  const step = 80;
  for (let x = 0; x <= width; x += step) {
    line(x, 0, x, height);
  }
  for (let y = 0; y <= height; y += step) {
    line(0, y, width, y);
  }
  pop();
}

function drawTrail() {
  if (trail.length < 2) return;
  push();
  noFill();
  strokeWeight(3);
  for (let i = 1; i < trail.length; i++) {
    const t = i / (trail.length - 1);
    const hueA = color('#22d3ee');
    const hueB = color('#7c3aed');
    stroke(lerpColor(hueA, hueB, t));
    const prev = trail[i - 1];
    const curr = trail[i];
    line(
      (prev.x - pose.x) * SCALE,
      (prev.y - pose.y) * SCALE,
      (curr.x - pose.x) * SCALE,
      (curr.y - pose.y) * SCALE
    );
  }
  pop();
}

function drawRobot() {
  push();
  translate(0, 0);
  rotate(pose.phi);

  const bodyLength = (ROBOT_DIM.length / 100) * SCALE;
  const bodyWidth = (ROBOT_DIM.width / 100) * SCALE;
  const rearTrack = (ROBOT_DIM.rearTrack / 100) * SCALE;
  const rearWheelRadius = (ROBOT_DIM.rearWheelRadius / 100) * SCALE;
  const rearWheelWidth = (ROBOT_DIM.rearWheelWidth / 100) * SCALE;
  const frontWheelRadius = (ROBOT_DIM.frontWheelRadius / 100) * SCALE;
  const frontWheelWidth = (ROBOT_DIM.frontWheelWidth / 100) * SCALE;

  // Corpo
  fill(20, 30, 60);
  stroke('#7c3aed');
  strokeWeight(2);
  rectMode(CENTER);
  rect(0, 0, bodyLength, bodyWidth, 12);

  // Indicador frontal
  fill('#22d3ee');
  noStroke();
  triangle(
    bodyLength / 2,
    0,
    bodyLength / 2 - 12,
    bodyWidth / 3,
    bodyLength / 2 - 12,
    -bodyWidth / 3
  );

  // Rodas traseiras
  fill('#0f172a');
  stroke('#22d3ee');
  strokeWeight(2);
  rect(
    -bodyLength / 4,
    rearTrack / 2,
    rearWheelWidth,
    rearWheelRadius * 2,
    6
  );
  rect(
    -bodyLength / 4,
    -rearTrack / 2,
    rearWheelWidth,
    rearWheelRadius * 2,
    6
  );

  // Rodas dianteiras
  rect(
    bodyLength / 3,
    rearTrack / 2,
    frontWheelWidth,
    frontWheelRadius * 2,
    6
  );
  rect(
    bodyLength / 3,
    -rearTrack / 2,
    frontWheelWidth,
    frontWheelRadius * 2,
    6
  );
  pop();
}

function applyPoseUpdate(newPose) {
  if (!Number.isFinite(newPose.x) || !Number.isFinite(newPose.y) || !Number.isFinite(newPose.phi)) {
    return;
  }

  if (lastPose) {
    const dx = newPose.x - lastPose.x;
    const dy = newPose.y - lastPose.y;
    totalDistance += sqrt(dx * dx + dy * dy);
  }

  pose = newPose;
  lastPose = { ...pose };

  trail.push({ x: pose.x, y: pose.y, phi: pose.phi });
  if (trail.length > TRAIL_LIMIT) {
    trail.shift();
  }

  xLabel.html(`${pose.x.toFixed(3)} m`);
  yLabel.html(`${pose.y.toFixed(3)} m`);
  headingLabel.html(`${degrees(pose.phi).toFixed(1)} °`);
  distanceLabel.html(`${totalDistance.toFixed(2)} m`);
}

function connectToBroker() {
  const host = select('#host').value();
  const port = Number(select('#port').value());
  const username = select('#username').value();
  const password = select('#password').value();
  const odomTopic = select('#odomTopic').value();

  if (client) {
    client.end(true);
    client = null;
  }

  const url = `wss://${host}:${port}/mqtt`;
  statusLabel.html('Conectando...');
  statusLabel.removeClass('ok');

  client = mqtt.connect(url, {
    username,
    password,
    clean: true,
    reconnectPeriod: 3000,
  });

  client.on('connect', () => {
    statusLabel.html('Conectado');
    statusLabel.addClass('ok');
    client.subscribe(odomTopic, { qos: 0 }, (err) => {
      if (err) {
        statusLabel.html('Erro ao inscrever no tópico');
        statusLabel.removeClass('ok');
      }
    });
  });

  client.on('reconnect', () => {
    statusLabel.html('Reconectando...');
    statusLabel.removeClass('ok');
  });

  client.on('error', (error) => {
    console.error('MQTT error', error);
    statusLabel.html('Erro na conexão');
    statusLabel.removeClass('ok');
  });

  client.on('message', (topic, payload) => {
    try {
      const msg = JSON.parse(payload.toString());
      applyPoseUpdate({
        x: Number(msg.x),
        y: Number(msg.y),
        phi: Number(msg.phi),
      });
    } catch (err) {
      console.warn('Mensagem de odometria inválida', err);
    }
  });
}

function resetPath() {
  pose = { x: 0, y: 0, phi: 0 };
  lastPose = { ...pose };
  trail = [];
  totalDistance = 0;
  xLabel.html('0.000 m');
  yLabel.html('0.000 m');
  headingLabel.html('0.0 °');
  distanceLabel.html('0.00 m');
}
