import processing.core.PVector;
import processing.data.IntDict;
import processing.data.Table;
import processing.data.TableRow;
import processing.data.JSONObject;

// ============================================================
//  PARAMÈTRES GLOBAUX & CONTRAT MOTEUR
// ============================================================
boolean exporterFrames = true;   
float timeSpeed      = 0.4;   
float gameTime       = 0;       
boolean matchTermine  = false;

String workDir = "";
String pathLogo1 = "equipe1.png";
String pathLogo2 = "equipe2.png";

// ---- TOILE VIRTUELLE HD ----
PGraphics hd;
int hdWidth = 1080;
int hdHeight = 1920;

// ---- ARÈNE ----
PVector centre;
float rayonCercle     = 400; 
float angleRotation   = 0;
float vitesseRotation = 0.04;   

float goalWidth = 110; 
float goalDepth = 120; 

// ---- AUDIO / LOG ----
Table audioLog;

// ---- EFFETS ----
float shakeIntensity = 0;
GoalFlash goalFlash;
ArrayList<Particle> particles;

// ---- ÉQUIPES ----
TeamBall eq1, eq2;

// ============================================================
void setup() {
  size(540, 960); // Sans P2D pour une compatibilité maximale sur VPS Linux
  hd = createGraphics(hdWidth, hdHeight);
  frameRate(60);

  // --- LECTURE DU FICHIER JOB.JSON FOURNI PAR LE MOTEUR ---
  if (args != null && args.length > 0) {
    println("[Processing] Lecture du job : " + args[0]);
    JSONObject job = loadJSONObject(args[0]);
    JSONObject resources = job.getJSONObject("resources");
    
    pathLogo1 = resources.getString("home_logo");
    pathLogo2 = resources.getString("away_logo");
    workDir   = job.getString("work_dir");
  } else {
    workDir = sketchPath();
  }

  centre = new PVector(hdWidth / 2.0, hdHeight / 2.0 + 160);

  audioLog = new Table();
  audioLog.addColumn("frame");
  audioLog.addColumn("sound");

  // --- Logos HD depuis assets/logos/ ---
  PImage img1 = loadImage(pathLogo1);
  PImage img2 = loadImage(pathLogo2);
  if (img1 != null) img1.resize(250, 0); 
  if (img2 != null) img2.resize(250, 0);

  // --- Couleurs ---
  color[] cols1 = extraireCouleurs(img1);
  color[] cols2 = extraireCouleurs(img2);

  // --- Création des balles ---
  eq1 = new TeamBall(img1, cols1[0], cols1[1], centre.x - 140, centre.y,  16, -6);
  eq2 = new TeamBall(img2, cols2[0], cols2[1], centre.x + 140, centre.y, -16,  6);

  goalFlash = new GoalFlash();
  particles = new ArrayList<Particle>();
}

// ============================================================
void draw() {
  if (!matchTermine) {
    gameTime += timeSpeed;
    angleRotation += vitesseRotation;
    if (gameTime > 91) {
      gameTime = 90;
      matchTermine = true;
    }
  }

  hd.beginDraw();
  hd.smooth(8);
  
  hd.background(34, 135, 55);
  dessinerPelouse(hd);
  goalFlash.display(hd);

  hd.pushMatrix();
  if (shakeIntensity > 0.5) {
    hd.translate(random(-shakeIntensity, shakeIntensity), random(-shakeIntensity, shakeIntensity));
    shakeIntensity *= 0.82;
  } else {
    shakeIntensity = 0;
  }

  dessinerArene(hd);

  if (!matchTermine) {
    eq1.update();
    eq2.update();
    gererCollision(eq1, eq2);
  }

  for (int i = particles.size() - 1; i >= 0; i--) {
    Particle p = particles.get(i);
    p.update();
    p.display(hd);
    if (p.isDead()) particles.remove(i);
  }

  eq1.display(hd);
  eq2.display(hd);
  hd.popMatrix();

  dessinerScoreboard(hd);
  hd.endDraw();

  imageMode(CORNER);
  image(hd, 0, 0, width, height);

  // EXPORTATION DANS LE DOSSIER TEMPORAIRE DU JOB
  if (exporterFrames) {
    if (!matchTermine) {
      hd.save(workDir + "/frames/frame-" + nf(frameCount, 4) + ".png");
    } else {
      saveTable(audioLog, workDir + "/audio_log.csv");
      println("      [ Processing ] Export terminé ! Frames et audio_log.csv générés.");
      exit();
    }
  }
}

// ============================================================
//  PHYSIQUE ET CLASSES (Identiques à ton code original)
// ============================================================
void gererCollision(TeamBall b1, TeamBall b2) {
  PVector d = PVector.sub(b1.pos, b2.pos);
  float dist = d.mag();
  float minD = b1.r + b2.r;
  if (dist >= minD) return;

  float overlap = minD - dist;
  PVector corr = d.copy().normalize().mult(overlap / 2.0);
  b1.pos.add(corr);
  b2.pos.sub(corr);

  PVector normale = d.copy().normalize();
  PVector relVel  = PVector.sub(b1.vel, b2.vel);
  float   dot     = relVel.dot(normale);
  if (dot > 0) return;

  float rest    = 1.05;
  float impulse = -(1 + rest) * dot / 2.0;
  b1.vel.add(PVector.mult(normale, impulse));
  b2.vel.sub(PVector.mult(normale, impulse));

  b1.vel.rotate(random(-0.06, 0.06));
  b2.vel.rotate(random(-0.06, 0.06));

  spawnParticles(PVector.lerp(b1.pos, b2.pos, 0.5), 15, color(255, 230, 100));
  enregistrerSon("bounce");
}

class TeamBall {
  PVector pos, vel;
  float   r     = 55; 
  color   c1, c2;
  PImage  logo;
  int     score = 0;

  float displayAngle = 0;
  float targetAngle  = 0;

  TeamBall(PImage img, color col1, color col2, float x, float y, float vx, float vy) {
    logo = img; c1 = col1; c2 = col2;
    pos  = new PVector(x, y);
    vel  = new PVector(vx, vy);
  }

  void update() {
    pos.add(vel);

    float speed = vel.mag();
    if (speed < 11)  vel.setMag(11);
    if (speed > 26) vel.setMag(26);

    targetAngle = vel.heading();
    float diff = targetAngle - displayAngle;
    while (diff >  PI) diff -= TWO_PI;
    while (diff < -PI) diff += TWO_PI;
    displayAngle += diff * 0.04;  

    PVector lp = PVector.sub(pos, centre);
    lp.rotate(-angleRotation);
    PVector lv = vel.copy();
    lv.rotate(-angleRotation);

    float d = lp.mag();

    if (d + r >= rayonCercle) {
      boolean inGoalArc = (lp.x > 0 && abs(lp.y) < goalWidth / 2.0 - r * 0.3);

      if (inGoalArc) {
        if (lp.y < -goalWidth / 2.0 + r) {
          lp.y = -goalWidth / 2.0 + r;
          lv.y *= -0.85;
          enregistrerSon("bounce");
        } else if (lp.y > goalWidth / 2.0 - r) {
          lp.y = goalWidth / 2.0 - r;
          lv.y *= -0.85;
          enregistrerSon("bounce");
        }
        
        if (lp.x + r > rayonCercle + goalDepth - 4) {
          score++;
          shakeIntensity = 35; 
          goalFlash.trigger(c1);
          spawnParticles(pos, 60, c1);
          spawnParticles(pos, 40, c2);
          enregistrerSon("goal");
          reset();
          return;
        }
      } else {
        PVector normale = lp.copy().normalize();
        lp = PVector.mult(normale, rayonCercle - r);
        float dot = lv.dot(normale);
        lv.sub(PVector.mult(normale, 2 * dot));
        lv.rotate(random(-0.08, 0.08));
        spawnParticles(pos, 5, color(255, 150));
        enregistrerSon("bounce");
      }
    }

    lp.rotate(angleRotation);
    pos = PVector.add(centre, lp);
    lv.rotate(angleRotation);
    vel = lv;
  }

  void display(PGraphics g) {
    g.pushMatrix();
    g.translate(pos.x, pos.y);

    g.noStroke();
    g.fill(0, 80);
    g.ellipse(8, 12, r * 2 + 6, r * 2 + 6);

    g.pushMatrix();
    g.rotate(displayAngle);

    g.noStroke();
    g.fill(c1);
    g.arc(0, 0, r * 2, r * 2, PI / 2, PI * 3 / 2);   
    g.fill(c2);
    g.arc(0, 0, r * 2, r * 2, -PI / 2, PI / 2);        

    g.stroke(255, 220);
    g.strokeWeight(4);
    g.line(0, -r, 0, r);

    g.noFill();
    g.stroke(255, 80);
    g.strokeWeight(3);
    g.ellipse(0, 0, r * 2, r * 2);

    if (logo != null) {
      g.pushMatrix();
      g.rotate(-displayAngle);
      g.imageMode(CENTER);
      g.image(logo, 0, 0, r * 1.5, r * 1.5);
      g.popMatrix();
    }

    g.popMatrix();
    g.popMatrix();
  }

  void reset() {
    pos.set(centre.x, centre.y);
    float ang = random(TWO_PI);
    float spd = random(16, 22);
    vel.set(cos(ang) * spd, sin(ang) * spd);
  }
}

void dessinerScoreboard(PGraphics g) {
  int    min     = floor(gameTime);
  int    sec     = floor((gameTime % 1) * 60);
  String timeStr = nf(min, 2) + "'" + nf(sec, 2);

  g.pushStyle();
  g.imageMode(CENTER);
  g.rectMode(CENTER);
  g.textAlign(CENTER, CENTER);
  g.noStroke();

  float cardW  = 960;         
  float cardH  = 160;                
  float cardX  = hdWidth / 2.0;
  float cardY  = 300;

  g.fill(0, 100);
  g.rect(cardX + 4, cardY + 10, cardW, cardH, 40);

  g.fill(13, 17, 26, 250);
  g.rect(cardX, cardY, cardW, cardH, 35);

  g.fill(eq1.c1);
  g.rect(cardX - cardW/4 + 10, cardY + cardH/2 - 4, cardW/2 - 50, 8, 4);
  g.fill(eq2.c1);
  g.rect(cardX + cardW/4 - 10, cardY + cardH/2 - 4, cardW/2 - 50, 8, 4);

  float logoX1 = cardX - cardW/2 + 100;
  float logoX2 = cardX + cardW/2 - 100;

  g.fill(255, 10); g.ellipse(logoX1, cardY, 110, 110);
  if (eq1.logo != null) g.image(eq1.logo, logoX1, cardY, 90, 90);

  g.fill(255, 10); g.ellipse(logoX2, cardY, 110, 110);
  if (eq2.logo != null) g.image(eq2.logo, logoX2, cardY, 90, 90);

  g.fill(255);
  g.textSize(90);
  g.text(eq1.score, cardX - 220, cardY - 5);
  g.text(eq2.score, cardX + 220, cardY - 5);

  g.fill(0, 120);
  g.rect(cardX, cardY, 190, 70, 15);

  g.fill(255, 230, 50);
  g.textSize(45);
  g.text(timeStr, cardX, cardY - 4);

  g.fill(60, 220, 100);
  g.ellipse(cardX - 40, cardY + 55, 14, 14);
  g.fill(255, 200);
  g.textSize(22);
  g.text("LIVE", cardX + 10, cardY + 52);

  g.popStyle();
}

void dessinerArene(PGraphics g) {
  g.pushMatrix();
  g.translate(centre.x, centre.y);
  g.rotate(angleRotation);

  float gapAngle = atan2(goalWidth / 2.0, rayonCercle);

  g.noFill();
  for (int i = 3; i >= 1; i--) {
    g.strokeWeight(i * 8);
    g.stroke(255, 255, 255, 30 / i);
    g.arc(0, 0, rayonCercle * 2, rayonCercle * 2, gapAngle, TWO_PI - gapAngle);
  }

  g.strokeWeight(8);
  g.stroke(255);
  g.arc(0, 0, rayonCercle * 2, rayonCercle * 2, gapAngle, TWO_PI - gapAngle);

  float xD  = cos(gapAngle) * rayonCercle;
  float yH  = -goalWidth / 2.0;
  float yB  =  goalWidth / 2.0;
  float xFd =  xD + goalDepth;

  g.noStroke();
  g.fill(255, 255, 255, 18);
  g.beginShape();
  g.vertex(xD, yH); g.vertex(xFd, yH);
  g.vertex(xFd, yB); g.vertex(xD, yB);
  g.endShape(CLOSE);

  g.stroke(255, 100);
  g.strokeWeight(2);
  for (float y = yH; y <= yB; y += 20) g.line(xD, y, xFd, y);
  for (float x = xD; x <= xFd; x += 20) g.line(x, yH, x, yB);

  g.stroke(255);
  g.strokeWeight(8);
  g.line(xD, yH, xFd, yH);
  g.line(xD, yB, xFd, yB);
  g.line(xFd, yH, xFd, yB);

  g.fill(255);
  g.noStroke();
  g.ellipse(xD, yH, 18, 18);
  g.ellipse(xD, yB, 18, 18);

  g.popMatrix();
}

void dessinerPelouse(PGraphics g) {
  g.noStroke();
  for (int i = 0; i < hdHeight; i += 120) {
    g.fill(0, 0, 0, 10);
    g.rectMode(CORNER);
    g.rect(0, i, hdWidth, 60);
  }
}

class GoalFlash {
  float alpha = 0;
  color flashColor = color(255);

  void trigger(color c) {
    flashColor = c;
    alpha = 200;
  }

  void display(PGraphics g) {
    if (alpha <= 0) return;
    g.noStroke();
    g.fill(red(flashColor), green(flashColor), blue(flashColor), alpha);
    g.rectMode(CORNER);
    g.rect(0, 0, hdWidth, hdHeight);
    alpha -= 8;
  }
}

class Particle {
  PVector pos, vel;
  float   life, maxLife, sz;
  color   c;

  Particle(PVector p, color col) {
    pos = p.copy();
    float a = random(TWO_PI);
    float s = random(5, 15); 
    vel = new PVector(cos(a) * s, sin(a) * s);
    maxLife = random(30, 50);
    life = maxLife;
    c = col;
    sz = random(8, 16); 
  }

  void update() {
    pos.add(vel);
    vel.mult(0.92);
    life--;
  }

  void display(PGraphics g) {
    float a = map(life, 0, maxLife, 0, 220);
    g.noStroke();
    g.fill(red(c), green(c), blue(c), a);
    g.ellipse(pos.x, pos.y, sz * (life / maxLife), sz * (life / maxLife));
  }
  boolean isDead() { return life <= 0; }
}

void spawnParticles(PVector p, int n, color c) {
  for (int i = 0; i < n; i++) particles.add(new Particle(p, c));
}

void enregistrerSon(String type) {
  TableRow row = audioLog.addRow();
  row.setInt("frame", frameCount);
  row.setString("sound", type);
}

color[] extraireCouleurs(PImage img) {
  if (img == null) return new color[]{ color(220, 30, 30), color(255, 255, 255) };
  img.loadPixels();
  IntDict counts = new IntDict();
  for (color c : img.pixels) {
    if (alpha(c) > 100) {
      int r = (int)(red(c) / 32) * 32;
      int g = (int)(green(c) / 32) * 32;
      int b = (int)(blue(c) / 32) * 32;
      counts.add(r + "_" + g + "_" + b, 1);
    }
  }
  counts.sortValuesReverse();
  String[] keys = counts.keyArray();
  color c1 = color(255), c2 = color(0);
  if (keys.length > 0) {
    String[] parts = keys[0].split("_");
    c1 = color(int(parts[0]), int(parts[1]), int(parts[2]));
  }
  for (int i = 1; i < keys.length; i++) {
    String[] parts = keys[i].split("_");
    color tmp = color(int(parts[0]), int(parts[1]), int(parts[2]));
    if (dist(red(c1), green(c1), blue(c1), red(tmp), green(tmp), blue(tmp)) > 70) {
      c2 = tmp; break;
    }
  }
  return new color[]{ c1, c2 };
}