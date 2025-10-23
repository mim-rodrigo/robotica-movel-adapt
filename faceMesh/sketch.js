let faceMesh;
let options = { maxFaces: 1, refineLandmarks: false, flipped: false };

let video;
let faces = [];

// --- Configurações do MQTT ---
const mqttUrl = "wss://0e51aa7bffcf45618c342e30a71338e8.s1.eu.hivemq.cloud:8884/mqtt";
const mqttTopic = "facemesh/offset"; // Tópico para publicar o dx

const connectOptions = {
  username: "hivemq.webclient.1761227941253",
  password: "&a9<Vzb3sC0A!6ZB>xTm",
  clientId: "p5js_client_" + parseInt(Math.random() * 1000)
};

let client; // Variável do cliente MQTT
let mqttInitialized = false; // Flag para conectar apenas uma vez
// ----------------------------


function preload() {
  faceMesh = ml5.faceMesh(options);
}

function setup() {
  createCanvas(640, 480);
  video = createCapture(VIDEO);
  video.size(640, 480);
  video.hide();
  
  // ATENÇÃO: Removemos o 'faceMesh.detectStart()' daqui.
  // Vamos iniciá-lo apenas DEPOIS que o MQTT conectar.
}

// Callback para salvar as detecções
function gotFaces(results) {
  faces = results;
}

function draw() {
  // Sempre desenha a imagem da webcam
  image(video, 0, 0, width, height);

  // --- Bloco de Inicialização do MQTT ---
  // Só roda uma vez, no começo, para conectar
  if (!mqttInitialized) {
    
    // 1. Espera a biblioteca MQTT ser carregada
    if (typeof mqtt !== 'undefined') {
      
      console.log("Biblioteca 'mqtt' detectada! Inicializando...");
      
      // 2. Trava o loop de inicialização
      mqttInitialized = true; 
      
      // 3. Conecta ao Broker
      console.log("Conectando ao MQTT Broker: " + mqttUrl);
      client = mqtt.connect(mqttUrl, connectOptions);
      
      // 4. Define o que fazer QUANDO conectar
      client.on("connect", function() {
        console.log("MQTT Conectado!");
        
        // 5. AGORA SIM, inicia a detecção do faceMesh
        console.log("Iniciando detecção do faceMesh.");
        faceMesh.detectStart(video, gotFaces);
      });

      // Handlers de erro/reconexão
      client.on("error", function(err) {
        console.log("Falha ao conectar no MQTT: " + err);
        mqttInitialized = false; // Permite tentar de novo
      });
      client.on("reconnect", function() {
        console.log("MQTT Reconectando...");
      });
      
    } else {
      // Se a biblioteca ainda não carregou, mostra um aviso
      fill(255, 0, 0);
      textAlign(CENTER);
      textSize(20);
      text("Aguardando biblioteca MQTT...", width / 2, height / 2);
    }
    
    // Sai do draw() enquanto inicializa
    return; 
  }
  
  // --- Fim do Bloco de Inicialização ---

  
  // --- Início do seu Código de Draw ---
  // (Esta parte só roda DEPOIS que o MQTT foi inicializado)
  
  // desenha um alvo no centro da imagem
  const cx = width / 2;
  const cy = height / 2;
  stroke(255, 0, 0);
  strokeWeight(1);
  line(cx - 10, cy, cx + 10, cy);
  line(cx, cy - 10, cx, cy + 10);
  noStroke();

  // desenha todos os pontos do rosto (opcional)
  for (let i = 0; i < faces.length; i++) {
    const face = faces[i];
    for (let j = 0; j < face.keypoints.length; j++) {
      const kp = face.keypoints[j];
      fill(0, 255, 0);
      circle(kp.x, kp.y, 4);
    }
  }

  // se houver rosto, pegue a ponta do nariz (keypoint 4)
  if (faces.length > 0 && faces[0].keypoints.length > 4) {
    const nose = faces[0].keypoints[4];

    // destaque a ponta do nariz
    fill(255, 255, 0);
    stroke(0);
    strokeWeight(1);
    circle(nose.x, nose.y, 8);
    noStroke();

    // vetor centro -> nariz
    stroke(255, 255, 0);
    strokeWeight(2);
    line(cx, cy, nose.x, nose.y);
    noStroke();

    // diferença em pixels
    const dx = nose.x - cx;
    const dy = nose.y - cy;
    
    // --- MODIFICAÇÃO: ENVIAR DX VIA MQTT ---
    if (client && client.connected) {
      let payload = dx.toFixed(1); // Envia dx com 1 casa decimal
      client.publish(mqttTopic, payload);
      
      // Log no console (reutilizando seu timer)
      if (!this._lastLog || millis() - this._lastLog > 200) {
         console.log(`MQTT Enviado [${mqttTopic}]: ${payload}`);
         this._lastLog = millis();
      }
    }
    // --- FIM DA MODIFICAÇÃO ---

    // valores normalizados (-1 a +1)
    const nx = dx / (width / 2);
    const ny = dy / (height / 2);

    // print na tela
    fill(255);
    noStroke();
    rect(6, height - 56, 370, 50, 6); // fundo para legibilidade
    fill(0);
    textSize(14);
    text(
      `Nariz relativo ao centro: dx=${dx.toFixed(1)} px, dy=${dy.toFixed(1)} px`,
      12, height - 34
    );
    text(
      `Normalizado: x=${nx.toFixed(3)}, y=${ny.toFixed(3)}  (−1 esq/cima, +1 dir/baixo)`,
      12, height - 16
    );
    
  } else {
    // sem rosto detectado
    fill(255);
    noStroke();
    rect(6, height - 26, 160, 20, 6);
    fill(0);
    textSize(14);
    text("Sem rosto detectado…", 12, height - 12);
  }
}