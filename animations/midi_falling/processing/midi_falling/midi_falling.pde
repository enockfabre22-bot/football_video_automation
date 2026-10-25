import javax.sound.midi.*;
import java.io.File;
import java.io.PrintWriter;
import java.util.ArrayList;
import processing.data.JSONObject;

// ==========================================================
// CONFIGURATION GÉNÉRALE & EXPORT TIKTOK/SHORTS (9:16)
// ==========================================================
boolean exportMode = true;     // <--- METTRE SUR TRUE POUR EXPORTER LES IMAGES ET LE TXT
boolean fastTestMode = false;  // <--- ACTIVER (true) POUR UN TEST ULTRA-RAPIDE (30 FPS, JPG)
float MAX_DURATION_SEC = 3.0; // <--- LIMITE DE TEMPS : Coupe la vidéo après X secondes

// --- VARIABLES DYNAMIQUES (Remplacées par le job.json) ---
String SONG_TITLE = "Unknown Title";
String SONG_AUTHOR = "Unknown Author";
String MIDI_FILE = "secours.mid"; 
String BG_FILE = "fond_secours.jpg";      
String workDir = "";

int RENDER_SCALE = 2;          // Échelle interne (2 = 1080x1920)

// Dimensions de base au format 9:16 parfait
int W = 540; 
int H = 960; 

PImage fond;
Receiver synth;
ArrayList<NoteData> notes = new ArrayList<NoteData>();

PrintWriter noteLog;
long songDurationMs = 0; 
int targetFPS = 60; 

ArrayList<Particle> particles = new ArrayList<Particle>();
ArrayList<ImpactRipple> ripples = new ArrayList<ImpactRipple>();

color colorRight, colorLeft;
color[] activeKeyColors = new color[128]; 

boolean[] activeKeys = new boolean[128];
long startTime;
long startDelayMs; 
boolean isReady = false;

// 24 touches pour que le piano reste lisible en format vertical
int numWhiteKeys = 24; 
float keyWidth;
float pianoY;
float pianoHeight = 120; 
float speed = 0.1;       

float camScale = 1.0f;
float targetCamScale = 1.0f;
float camCenterX;
float targetCamCenterX;

void setup() {
  // Format Vertical 9:16 (1080x1920)
  size(1080, 1920); 
  
  // =======================================================
  // 1. LECTURE DU JOB.JSON (Le Cerveau Python)
  // =======================================================
  String jsonPath = (args != null && args.length > 0) ? args[0] : sketchPath("job.json");
  File file = new File(jsonPath);

  if (file.exists()) {
    try {
      JSONObject job = loadJSONObject(file);
      
      // Dossier de travail (où sauvegarder les images et le log)
      if (!job.isNull("work_dir")) {
        workDir = job.getString("work_dir");
      } else {
        workDir = file.getParent(); 
      }
      
      // On récupère TOUT (Textes + Fichiers) directement dans les metadata !
      if (!job.isNull("metadata")) {
        JSONObject meta = job.getJSONObject("metadata");
        SONG_TITLE = meta.getString("song_title", SONG_TITLE);
        SONG_AUTHOR = meta.getString("song_author", SONG_AUTHOR);
        MIDI_FILE = meta.getString("midi_file", MIDI_FILE);
        BG_FILE = meta.getString("bg_image", BG_FILE);
      }
      
      println("✅ JOB CHARGÉ : " + SONG_TITLE + " par " + SONG_AUTHOR);
      
    } catch(Exception e) {
      println("❌ ERREUR JSON : " + e.getMessage());
      workDir = sketchPath();
    }
  } else {
    println("⚠️ AUCUN JOB TROUVÉ. Mode manuel activé.");
    workDir = sketchPath();
  }

  // Création du dossier de frames dans le dossier de travail du serveur
  File fDir = new File(workDir + "/frames");
  if (!fDir.exists() && exportMode) fDir.mkdirs();

  // =======================================================
  // 2. INITIALISATION DE L'ANIMATION
  // =======================================================
  if (fastTestMode && exportMode) {
    targetFPS = 30; // Mode rapide : 2x moins d'images à calculer
  } else {
    targetFPS = 60; // Mode production : fluidité maximale
  }
  frameRate(targetFPS);
  
  if (exportMode) {
    // Sauvegarde du TXT dans le dossier de travail !
    noteLog = createWriter(workDir + "/notes_log.txt");
    if (fastTestMode) {
      println("MODE TEST RAPIDE ACTIF : Rendu en 30 FPS au format JPG.");
    } else {
      println("MODE PRODUCTION TIKTOK ACTIF : Rendu en 60 FPS (1080x1920) au format PNG Haute Qualité.");
    }
  }
  
  // Placement du piano plus haut pour laisser la place aux descriptions TikTok
  pianoY = H - 320; 
  startDelayMs = (long)(pianoY / speed) + 800;
  camCenterX = W / 2.0f;
  targetCamCenterX = W / 2.0f;
  
  color[] palette = {
    color(255, 89, 94), color(255, 146, 76), color(255, 202, 58), color(138, 201, 38), 
    color(25, 130, 196), color(106, 76, 147), color(255, 153, 200), color(0, 245, 212), 
    color(0, 187, 249), color(254, 228, 64), color(241, 91, 181), color(155, 93, 229), 
    color(255, 105, 180), color(0, 250, 154), color(221, 160, 221), color(127, 255, 212), 
    color(255, 127, 80), color(173, 255, 47), color(255, 218, 185), color(64, 224, 208)
  };
  
  int r1 = (int)random(palette.length);
  int r2 = (int)random(palette.length);
  while (r1 == r2) r2 = (int)random(palette.length);
  colorRight = palette[r1];
  colorLeft = palette[r2];
  
  // Chargement sécurisé de l'image de fond
  try {
    fond = loadImage(BG_FILE);
    if (fond == null && !BG_FILE.contains(":")) fond = loadImage(dataPath(BG_FILE));
    if (fond == null) {
      println("❌ ERREUR: Image de fond introuvable -> " + BG_FILE);
      fond = createImage(1080, 1920, RGB); // Image noire pour éviter le crash
    }
  } catch (Exception e) {
    println("Attention : Image de fond non trouvée.");
  }

  // Chargement sécurisé du fichier MIDI (Optimisé pour Serveur Linux)
  try {
    // On ne cherche à allumer la carte son que si on est sur PC (exportMode = false)
    if (!exportMode) {
      synth = MidiSystem.getReceiver();
      ShortMessage inst = new ShortMessage();
      inst.setMessage(ShortMessage.PROGRAM_CHANGE, 0, 0, 0); 
      synth.send(inst, -1);
    }
    
    File midiFile = new File(MIDI_FILE);
    if(!midiFile.exists() && !MIDI_FILE.contains(":")) midiFile = new File(dataPath(MIDI_FILE)); 
    
    // getSequence se contente de lire le fichier, pas besoin de carte son !
    Sequence seq = MidiSystem.getSequence(midiFile);
    parseMidi(seq);
    
    startTime = millis();
    isReady = true; // Le programme est débloqué !
  } catch (Exception e) {
    println("❌ Erreur de chargement MIDI : " + e.getMessage());
  }
}

void draw() {
  scale(RENDER_SCALE);

  if (fond != null) image(fond, 0, 0, W, H);
  else background(135, 206, 250); 
  
  if (!isReady) return;
  
  long elapsedMs;
  if (exportMode) {
    elapsedMs = (long)((frameCount / (float)targetFPS) * 1000.0f);
  } else {
    elapsedMs = millis() - startTime;
  }
  
  long currentMs = elapsedMs - startDelayMs;
  keyWidth = W / (float)numWhiteKeys;
  
  updateCamera(currentMs);
  
  pushMatrix();
  applyCamera();
  drawDashedLines(); 
  popMatrix();
  
  clip(0, 0, W, pianoY); 
  pushMatrix();
  applyCamera();
  updateAndDrawNotes(currentMs);
  popMatrix();
  noClip(); 
  
  pushMatrix();
  applyCamera();
  updateAndDrawFX();
  drawPiano();
  popMatrix();
  
  // Animation d'introduction adaptée à l'écran vertical
  drawIntroAnimation(elapsedMs);
  
  // Indicateur visuel discret du mode test
  if (fastTestMode && exportMode) {
    fill(20, 180);
    noStroke();
    rectMode(CORNER);
    rect(10, 10, 210, 30, 5);
    fill(255, 120, 0);
    textSize(11);
    textAlign(LEFT, CENTER);
    text("● MODE TEST RAPIDE (30 FPS)", 20, 25);
  }
  
  if (exportMode) {
    // Sauvegarde des frames dans le dossier de travail du serveur
    if (fastTestMode) {
      saveFrame(workDir + "/frames/frame-######.jpg"); 
    } else {
      saveFrame(workDir + "/frames/frame-######.png"); 
    }
    
    // CONDITION DE FIN : Soit la fin de la musique, soit la limite MAX_DURATION_SEC atteinte
    long maxAllowedMs = (long)(MAX_DURATION_SEC * 1000.0f);
    long targetEndMs = Math.min(songDurationMs + 3000, maxAllowedMs);
    
    if (currentMs > targetEndMs) {
      noteLog.flush();
      noteLog.close();
      println("▶ EXPORTATION TERMINÉE : Limite de temps ou fin de piste atteinte !");
      exit();
    }
  }
}

// ==========================================================
// ANIMATION D'INTRODUCTION CINÉMATIQUE
// ==========================================================
void drawIntroAnimation(long elapsedMs) {
  if (elapsedMs > 5500) return; 

  float introAlpha = 0;
  float yOffset = 0;
  float scaleVal = 1.0f;
  float lineWidth = 0;

  if (elapsedMs < 1000) {
    float t = elapsedMs / 1000.0f;
    float easeOut = 1.0f - pow(1.0f - t, 3); 
    introAlpha = easeOut * 255;
    yOffset = (1.0f - easeOut) * 50;
    lineWidth = easeOut * 120;
  } else if (elapsedMs < 4000) {
    introAlpha = 255;
    yOffset = 0;
    scaleVal = map(elapsedMs, 1000, 4000, 1.0f, 1.05f);
    lineWidth = 120;
  } else if (elapsedMs <= 5500) {
    float t = (elapsedMs - 4000) / 1500.0f;
    float easeIn = t * t * t; 
    introAlpha = (1.0f - t) * 255;
    yOffset = easeIn * -50;
    scaleVal = map(elapsedMs, 4000, 5500, 1.05f, 1.1f);
    lineWidth = (1.0f - t) * 120;
  }

  if (introAlpha > 0) {
    pushMatrix();
    // Centrage ajusté pour la disposition verticale
    translate(W / 2.0f, H / 3.0f + yOffset);
    scale(scaleVal);

    noStroke();
    fill(10, 10, 15, introAlpha * 0.65f); 
    rectMode(CENTER);
    rect(0, 0, 420, 130, 15); // Un peu plus étroit pour l'écran de téléphone

    stroke(255, introAlpha * 0.35f);
    strokeWeight(1.2);
    noFill();
    rect(0, 0, 420, 130, 15);
    
    rectMode(CORNER);

    stroke(colorRight, introAlpha * 0.9f);
    strokeWeight(2);
    line(-lineWidth / 2.0f, 5, lineWidth / 2.0f, 5);

    textAlign(CENTER, CENTER);
    fill(255, introAlpha);
    textSize(28); // Légèrement réduit
    text(SONG_TITLE, 0, -20);

    fill(200, introAlpha * 0.8f);
    textSize(16);
    text(SONG_AUTHOR, 0, 30);

    popMatrix();
  }
}

// ==========================================================
// CAMÉRA DYNAMIQUE CALIBRÉE POUR LE 9:16 (TIKTOK)
// ==========================================================
void updateCamera(long currentMs) {
  float minP = 127;
  float maxP = 0;
  boolean found = false;
  long lookAhead = 3500; 

  for (NoteData nd : notes) {
    if (nd.startTime < currentMs + lookAhead && (nd.startTime + nd.duration) > currentMs) {
      if (nd.pitch < minP) minP = nd.pitch;
      if (nd.pitch > maxP) maxP = nd.pitch;
      found = true;
    }
  }

  if (found) {
    float x1 = getXForPitch((int)minP);
    float x2 = getXForPitch((int)maxP) + getWidthForPitch((int)maxP);
    float w = x2 - x1;

    targetCamCenterX = x1 + w / 2.0f;

    // Adapté à l'étroitesse de l'écran TikTok
    if (w < W * 0.4f) {
      targetCamScale = 1.45f; 
    } else {
      targetCamScale = (W * 0.75f) / w;
      targetCamScale = constrain(targetCamScale, 1.0f, 1.45f);
    }

    if (w > W * 0.8f) {
      targetCamScale = 1.0f; 
      targetCamCenterX = W / 2.0f;
    }
  } else {
    targetCamScale = 1.0f;
    targetCamCenterX = W / 2.0f;
  }

  camScale = lerp(camScale, targetCamScale, 0.012f); 
  camCenterX = lerp(camCenterX, targetCamCenterX, 0.015f);

  float boundMargin = W / (2.0f * camScale);
  camCenterX = constrain(camCenterX, boundMargin, W - boundMargin);
}

void applyCamera() {
  translate(W / 2.0f, pianoY);
  scale(camScale);
  translate(-camCenterX, -pianoY);
}

// ==========================================================
// EFFET DE TRAIT CARTOON
// ==========================================================
float getStableNoise(float x, float y) {
  return (noise(x * 0.05f, y * 0.05f) - 0.5f) * 2.0f; 
}

void drawCleanCartoonNote(float x, float y, float w, float h, color fillCol, String noteName, long seed) {
  randomSeed((int)(seed % 1000000));
  noStroke();
  fill(fillCol);
  rect(x, y, w, h, 6); 
  
  fill(255, 110);
  rect(x + 3, y + 3, max(2, w * 0.18f), h - 6, 2);
  
  stroke(20);
  strokeWeight(2.5); 
  noFill();
  rect(x, y, w, h, 6);
  
  if (noteName != null) {
    boolean isThin = (w <= 16);
    textSize(isThin ? 9 : 12); 
    textAlign(CENTER, CENTER);
    float textY = y + h - 12; 
    if (h <= 45) textY = y + h / 2 - 2; 
    
    if (w > 8) {
      pushMatrix();
      translate(x + w / 2, textY);
      if (!isThin) rotate(radians(random(-3.5f, 3.5f))); 
      
      fill(255, 200);
      text(noteName, -1, -1); text(noteName, 1, -1);
      text(noteName, -1, 1);  text(noteName, 1, 1);
      fill(20);
      text(noteName, 0, 0);
      popMatrix();
    }
  }
}

// ==========================================================
// MOTEUR D'ANIMATION ET FICHIER TXT
// ==========================================================
void updateAndDrawNotes(long currentMs) {
  for (int i = 0; i < 128; i++) activeKeys[i] = false;
  
  for (NoteData nd : notes) {
    float timeDiff = nd.startTime - currentMs;
    float h = max(24, nd.duration * speed); 
    float y = pianoY - (timeDiff * speed); 
    
    if (timeDiff <= 0 && timeDiff > -nd.duration) {
      activeKeys[nd.pitch] = true;
      activeKeyColors[nd.pitch] = nd.noteColor; 
      
      if (!nd.played) {
        if (!exportMode) playNote(nd.pitch, nd.velocity);
        nd.played = true;
        triggerImpactFX(nd.pitch, nd.noteColor);
        
        if (exportMode && noteLog != null) {
          noteLog.println(frameCount + "," + nd.pitch);
        }
      }
    }
    
    if (timeDiff <= -nd.duration && !nd.finished) {
      if (!exportMode) stopNote(nd.pitch);
      nd.finished = true;
    }
    
    if (y > -h && y - h < H) {
      float x = getXForPitch(nd.pitch);
      float w = getWidthForPitch(nd.pitch);
      
      if (x >= 0 && x < W) {
        boolean isBlack = isBlackKey(nd.pitch);
        float pad = isBlack ? 1.0f : 2.0f; 
        drawCleanCartoonNote(x + pad, y - h, w - pad*2, h, nd.noteColor, getNoteName(nd.pitch), nd.startTime);
      }
    }
  }
}

// ==========================================================
// SYSTÈME DE PARTICULES & EFFETS D'IMPACT (FX)
// ==========================================================
void triggerImpactFX(int pitch, color fxCol) {
  float x = getXForPitch(pitch);
  float w = getWidthForPitch(pitch);
  float targetX = x + w / 2;
  
  ripples.add(new ImpactRipple(targetX, pianoY, fxCol));
  
  int particleCount = (int)random(8, 12); 
  for (int i = 0; i < particleCount; i++) {
    particles.add(new Particle(targetX, pianoY, fxCol));
  }
}

void updateAndDrawFX() {
  for (int i = ripples.size() - 1; i >= 0; i--) {
    ImpactRipple r = ripples.get(i);
    r.update();
    r.display();
    if (r.isDead()) ripples.remove(i);
  }
  for (int i = particles.size() - 1; i >= 0; i--) {
    Particle p = particles.get(i);
    p.update();
    p.display();
    if (p.isDead()) particles.remove(i);
  }
}

class Particle {
  float x, y, vx, vy, size, alpha, gravity = 0.12f;
  color col;
  int shapeType;

  Particle(float startX, float startY, color c) {
    x = startX + random(-10, 10);
    y = startY + random(-5, 0); 
    vx = random(-2.5f, 2.5f);
    vy = random(-5.0f, -1.5f); 
    size = random(3, 8);
    col = c;
    alpha = 255;
    shapeType = random(1) > 0.4 ? 0 : 1; 
  }

  void update() {
    x += vx; y += vy; vy += gravity; 
    alpha -= 6.5f; size = max(0, size - 0.05f); 
  }

  void display() {
    if (alpha <= 0) return;
    pushMatrix();
    translate(x, y);
    stroke(20, alpha);
    strokeWeight(1.5);
    fill(col, alpha);
    
    if (shapeType == 0) ellipse(0, 0, size, size);
    else {
      beginShape();
      vertex(0, -size); vertex(size*0.4f, -size*0.4f); vertex(size, 0);
      vertex(size*0.4f, size*0.4f); vertex(0, size); vertex(-size*0.4f, size*0.4f);
      vertex(-size, 0); vertex(-size*0.4f, -size*0.4f);
      endShape(CLOSE);
    }
    popMatrix();
  }
  boolean isDead() { return alpha <= 0 || size <= 0; }
}

class ImpactRipple {
  float x, y, radius, maxRadius, alpha, growSpeed;
  color col;

  ImpactRipple(float startX, float startY, color c) {
    x = startX; y = startY; radius = 2;
    maxRadius = random(25, 45); 
    col = c; alpha = 200; growSpeed = random(1.5f, 2.5f);
  }

  void update() {
    radius += growSpeed;
    alpha = map(radius, 0, maxRadius, 200, 0); 
  }

  void display() {
    if (alpha <= 0) return;
    stroke(col, alpha);
    strokeWeight(2.0f);
    noFill();
    
    beginShape();
    int points = 16;
    for (int i = 0; i < points; i++) {
      float angle = TWO_PI * i / points;
      float r = radius + (noise(x + cos(angle)*0.1f, y + sin(angle)*0.1f, frameCount*0.1f) - 0.5f) * 4.0f;
      vertex(x + cos(angle) * r * 1.5f, y + sin(angle) * r * 0.4f);
    }
    endShape(CLOSE);
  }
  boolean isDead() { return radius >= maxRadius || alpha <= 0; }
}

// ==========================================================
// DESSIN DU CLAVIER (C3 = PITCH 48)
// ==========================================================
void drawPiano() {
  for (int i = 0; i < numWhiteKeys; i++) {
    int pitch = 48 + getWhitePitchOffset(i); 
    boolean isActive = activeKeys[pitch];
    float x = i * keyWidth;
    
    stroke(20);
    strokeWeight(2.5); 
    
    if (isActive) fill(activeKeyColors[pitch]); 
    else fill(255);
    
    rect(x, pianoY, keyWidth, pianoHeight, 0, 0, 4, 4);
    
    if (isActive) {
      noStroke(); 
      fill(255, 120); 
      rect(x + 2, pianoY + 2, max(2, keyWidth * 0.15f), pianoHeight - 6, 2);
    }
    
    if (pitch % 12 == 0) {
      int octave = (pitch / 12) - 1; 
      fill(100); textSize(10); textAlign(CENTER, CENTER);
      text("C" + octave, x + keyWidth/2, pianoY + pianoHeight - 14);
    }
  }
  
  stroke(20);
  strokeWeight(3);
  line(0, pianoY, W, pianoY);
  
  float blackKeyWidth = keyWidth * 0.75f;
  float blackKeyHeight = pianoHeight * 0.6;
  
  for (int i = 0; i < numWhiteKeys - 1; i++) {
    int pitch = 48 + getWhitePitchOffset(i);
    int patternIdx = pitch % 12;
    
    if (patternIdx == 0 || patternIdx == 2 || patternIdx == 5 || patternIdx == 7 || patternIdx == 9) {
      int blackPitch = pitch + 1;
      boolean isActive = activeKeys[blackPitch];
      float x = (i + 1) * keyWidth - (blackKeyWidth / 2);
      
      stroke(20);
      strokeWeight(2.5);
      
      if (isActive) fill(activeKeyColors[blackPitch]); 
      else fill(35);
      
      rect(x, pianoY, blackKeyWidth, blackKeyHeight, 0, 0, 3, 3);
      
      if (isActive) {
        noStroke();
        fill(255, 80);
        rect(x + 1.5f, pianoY + 1.5f, max(1, blackKeyWidth * 0.15f), blackKeyHeight - 4, 1);
      }
    }
  }
}

// ==========================================================
// SÉPARATION ESTHÉTIQUE DES OCTAVES
// ==========================================================
void drawDashedLines() {
  stroke(255, 80); 
  strokeWeight(1.2);
  
  float dashLen = 10;  
  float gapLen = 15;   
  
  for (int i = 1; i < numWhiteKeys; i++) {
    int pitch = 48 + getWhitePitchOffset(i);
    
    if (pitch % 12 == 0) {
      float lx = i * keyWidth;
      float currentY = 0;
      
      while (currentY < pianoY) {
        line(lx, currentY, lx, min(currentY + dashLen, pianoY));
        currentY += dashLen + gapLen;
      }
    }
  }
}

// ==========================================================
// CALCULS MUSICAUX ET LECTURE MIDI (DEPUIS C3 / PITCH 48)
// ==========================================================
int getWhitePitchOffset(int index) {
  int[] majorScale = {0, 2, 4, 5, 7, 9, 11};
  return (index / 7) * 12 + majorScale[index % 7];
}
boolean isBlackKey(int pitch) {
  int rel = pitch % 12;
  return (rel==1 || rel==3 || rel==6 || rel==8 || rel==10);
}
float getXForPitch(int pitch) {
  int relPitch = pitch - 48; 
  if (relPitch < 0) return -100;
  int octaves = relPitch / 12;
  int noteInOctave = relPitch % 12;
  float[] indexMap = {0, 0.5f, 1, 1.5f, 2, 3, 3.5f, 4, 4.5f, 5, 5.5f, 6};
  float floatIndex = (octaves * 7) + indexMap[noteInOctave];
  if (floatIndex == Math.floor(floatIndex)) return floatIndex * keyWidth; 
  else return ((int)Math.floor(floatIndex) + 1) * keyWidth - ((keyWidth * 0.75f) / 2); 
}
float getWidthForPitch(int pitch) {
  return isBlackKey(pitch) ? (keyWidth * 0.75f) : keyWidth;
}
String getNoteName(int pitch) {
  String[] names = {"C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B"};
  return names[pitch % 12];
}
void playNote(int pitch, int vel) {
  if (synth != null) {
    try {
      ShortMessage msg = new ShortMessage();
      msg.setMessage(ShortMessage.NOTE_ON, 0, pitch, vel);
      synth.send(msg, -1);
    } catch (Exception e) {}
  }
}
void stopNote(int pitch) {
  if (synth != null) {
    try {
      ShortMessage msg = new ShortMessage();
      msg.setMessage(ShortMessage.NOTE_OFF, 0, pitch, 0);
      synth.send(msg, -1);
    } catch (Exception e) {}
  }
}
void parseMidi(Sequence seq) {
  double msPerTick = seq.getMicrosecondLength() / (1000.0 * seq.getTickLength());
  int trackIdx = 0;
  for (Track track : seq.getTracks()) {
    for (int i = 0; i < track.size(); i++) {
      MidiEvent ev = track.get(i);
      MidiMessage msg = ev.getMessage();
      if (msg instanceof ShortMessage) {
        ShortMessage sm = (ShortMessage) msg;
        int cmd = sm.getCommand();
        int pitch = sm.getData1();
        int vel = sm.getData2();
        long timeMs = (long)(ev.getTick() * msPerTick);
        
        if (cmd == ShortMessage.NOTE_ON && vel > 0) {
          notes.add(new NoteData(pitch, timeMs, vel, trackIdx));
        } else if (cmd == ShortMessage.NOTE_OFF || (cmd == ShortMessage.NOTE_ON && vel == 0)) {
          for (int n = notes.size() - 1; n >= 0; n--) {
            NoteData nd = notes.get(n);
            if (nd.pitch == pitch && nd.duration == 0 && nd.trackNum == trackIdx) {
              nd.duration = timeMs - nd.startTime;
              break;
            }
          }
        }
      }
    }
    trackIdx++;
  }
  for (NoteData nd : notes) {
    if (nd.duration == 0) nd.duration = 200;
    if (nd.startTime + nd.duration > songDurationMs) {
      songDurationMs = nd.startTime + nd.duration;
    }
  }
  ArrayList<Integer> activeTracks = new ArrayList<Integer>();
  for (NoteData nd : notes) {
    if (!activeTracks.contains(nd.trackNum)) activeTracks.add(nd.trackNum);
  }
  boolean splitByPitch = (activeTracks.size() <= 1);
  int trackRight = -1;
  if (!splitByPitch) trackRight = activeTracks.get(0); 
  
  for (NoteData nd : notes) {
    if (splitByPitch) nd.noteColor = (nd.pitch >= 60) ? colorRight : colorLeft;
    else nd.noteColor = (nd.trackNum == trackRight) ? colorRight : colorLeft;
  }
}

class NoteData {
  int pitch, velocity, trackNum;
  long startTime, duration = 0;
  boolean played = false, finished = false;
  color noteColor; 
  NoteData(int pitch, long startTime, int velocity, int trackNum) {
    this.pitch = pitch; this.startTime = startTime; 
    this.velocity = velocity; this.trackNum = trackNum;
  }
}