import processing.core.PVector;
import processing.data.Table;
import processing.data.TableRow;
import processing.data.JSONObject;
import java.io.File;

// ============================================================
//  PARAMÈTRES GLOBAUX & CONFIGURATION
// ============================================================
boolean exporterFrames = true; // Mets à 'true' pour exporter le CSV et les PNG
String workDir       = "";
String pathImg1      = "Chelsea.png";
String pathImg2      = "Arsenal.png";
String name1         = "Chelsea";
String name2         = "Arsenal";

// ---- TOILE VIRTUELLE HD ----
PGraphics hd;
int hdWidth = 1080;
int hdHeight = 1920;

// ---- ARÈNE ----
float arenaX, arenaY, arenaW, arenaH;
color arenaBorder = color(100, 100, 255);
float arenaOffsetX = 0, arenaOffsetY = 0;

// ---- JEU ----
Player p1, p2;
ArrayList<Saw> fieldSaws;
ArrayList<Heart> fieldHearts;
ArrayList<HitParticle> hitParticles; 
ArrayList<DeathParticle> deathParticles; 

int maxLives = 3;
float sawSpawnTimer = 0;
float heartSpawnTimer = 0;
int hitStopTimer = 0; 
boolean matchTermine = false;
int endTimer = 0;

// ---- AUDIO / LOG ----
Table audioLog;

// ============================================================
void setup() {
  size(540, 960);
  hd = createGraphics(hdWidth, hdHeight);
  frameRate(60);

  // --- LECTURE DU JOB.JSON ---
  // On vérifie d'abord si Python nous a passé le chemin via args[0]
  String jsonPath = (args != null && args.length > 0) ? args[0] : sketchPath("job.json");
  File file = new File(jsonPath);

  if (file.exists()) {
    try {
      JSONObject job = loadJSONObject(file);
      
      // 1. Récupération des images (dans le tiroir "resources")
      if (!job.isNull("resources")) {
        JSONObject resources = job.getJSONObject("resources");
        pathImg1 = resources.getString("home_logo");
        pathImg2 = resources.getString("away_logo");
      }
      
      // 2. Récupération des noms (dans le tiroir "match" !)
      if (!job.isNull("match")) {
        JSONObject matchObj = job.getJSONObject("match");
        name1 = matchObj.getString("home");
        name2 = matchObj.getString("away");
        
        // Mettre la première lettre en majuscule (ex: angers -> Angers)
        if (name1.length() > 0) name1 = name1.substring(0, 1).toUpperCase() + name1.substring(1);
        if (name2.length() > 0) name2 = name2.substring(0, 1).toUpperCase() + name2.substring(1);
      }
      
      // 3. Dossier de travail
      if (!job.isNull("work_dir")) {
        workDir = job.getString("work_dir");
      } else {
        workDir = file.getParent(); 
      }
      
      println("✅ JOB CHARGÉ : " + name1 + " VS " + name2);
      
    } catch(Exception e) {
      println("❌ ERREUR JSON : " + e.getMessage());
      workDir = sketchPath();
    }
  } else {
    println("⚠️ AUCUN JOB TROUVÉ. Mode manuel activé.");
    workDir = sketchPath();
  }

  // --- CRÉATION DU DOSSIER FRAMES ---
  File fDir = new File(workDir + "/frames");
  if (!fDir.exists() && exporterFrames) fDir.mkdirs();


  audioLog = new Table();
  audioLog.addColumn("frame");
  audioLog.addColumn("sound");

  arenaW = 900;
  arenaH = 1000;
  arenaX = (hdWidth - arenaW) / 2;
  arenaY = (hdHeight - arenaH) / 2 + 100;

PImage img1 = loadImage(pathImg1);
  if (img1 == null) {
    println("❌ ERREUR: Impossible de charger l'image 1 : " + pathImg1);
    img1 = createImage(100, 100, RGB); // Image noire de secours pour éviter le crash
  }
  
  PImage img2 = loadImage(pathImg2);
  if (img2 == null) {
    println("❌ ERREUR: Impossible de charger l'image 2 : " + pathImg2);
    img2 = createImage(100, 100, RGB);
  }

  p1 = new Player(img1, color(30, 80, 255), arenaX + 200, arenaY + arenaH/2, 8, -6);
  p2 = new Player(img2, color(255, 40, 40), arenaX + arenaW - 200, arenaY + arenaH/2, -8, 6);

  fieldSaws = new ArrayList<Saw>();
  fieldHearts = new ArrayList<Heart>();
  hitParticles = new ArrayList<HitParticle>();
  deathParticles = new ArrayList<DeathParticle>();
  
  heartSpawnTimer = random(200, 500);
}

// ============================================================
void draw() {
  hd.beginDraw();
  hd.background(15, 10, 20);

  dessinerUI(hd);
  
  hd.pushMatrix();
  if (hitStopTimer > 0) {
    hd.translate(random(-12, 12), random(-12, 12));
  }

  if (hitStopTimer <= 0) {
    updateArenaPhysics();
  }
  dessinerArene(hd);

  if (hitStopTimer <= 0) {
    
    // --- Phase de Jeu Actif ---
    if (!matchTermine) {
      sawSpawnTimer--;
      if (sawSpawnTimer < 0 && fieldSaws.size() < 2 && !p1.hasSaw && !p2.hasSaw) {
        fieldSaws.add(new Saw(random(arenaX + 100, arenaX + arenaW - 100), random(arenaY + 100, arenaY + arenaH - 100)));
        sawSpawnTimer = random(120, 300);
      }

      heartSpawnTimer--;
      if (heartSpawnTimer < 0 && fieldHearts.size() < 1) {
        fieldHearts.add(new Heart(random(arenaX + 100, arenaX + arenaW - 100), random(arenaY + 100, arenaY + arenaH - 100)));
        heartSpawnTimer = random(300, 700);
      }

      for (int i = fieldSaws.size() - 1; i >= 0; i--) {
        Saw s = fieldSaws.get(i);
        s.update();
        if (!p1.hasSaw && !p1.isDead && s.checkCollision(p1.pos, p1.r)) { 
          p1.equipSaw(); 
          fieldSaws.remove(i); 
          enregistrerSon("saw"); // <-- Modifié ici 
        } 
        else if (!p2.hasSaw && !p2.isDead && s.checkCollision(p2.pos, p2.r)) { 
          p2.equipSaw(); 
          fieldSaws.remove(i); 
          enregistrerSon("saw"); // <-- Modifié ici
        }
      }

      for (int i = fieldHearts.size() - 1; i >= 0; i--) {
        Heart h = fieldHearts.get(i);
        h.update();
        boolean pickedUp = false;
        if (!p1.isDead && h.checkCollision(p1.pos, p1.r)) { 
          if (p1.lives < maxLives) p1.lives++; 
          pickedUp = true; 
        } 
        else if (!p2.isDead && h.checkCollision(p2.pos, p2.r)) { 
          if (p2.lives < maxLives) p2.lives++; 
          pickedUp = true; 
        }
        if (pickedUp) { 
          fieldHearts.remove(i); 
          enregistrerSon("heart"); // <-- Modifié ici
        }
      }

      gererCollisionJoueurs(p1, p2);

      // Vérification des morts
      if (p1.lives <= 0 && !p1.isDead) { p1.die(); matchTermine = true; }
      if (p2.lives <= 0 && !p2.isDead) { p2.die(); matchTermine = true; }
      
    } else {
      // --- Phase de Fin (Après une mort) ---
      endTimer++;
    }

    // Mise à jour des joueurs en vie
    if (!p1.isDead) p1.update();
    if (!p2.isDead) p2.update();
    
    // Mise à jour des particules
    for (int i = hitParticles.size() - 1; i >= 0; i--) {
      HitParticle p = hitParticles.get(i);
      p.update();
      if (p.life <= 0) hitParticles.remove(i);
    }
    for (int i = deathParticles.size() - 1; i >= 0; i--) {
      DeathParticle p = deathParticles.get(i);
      p.update();
      if (p.life <= 0) deathParticles.remove(i);
    }
    
  } else {
    hitStopTimer--; 
  }

  // ================= AFFICHAGE =================
  if (!matchTermine) {
    for (Saw s : fieldSaws) s.display(hd, false);
    for (Heart h : fieldHearts) h.display(hd);
  }

  if (!p1.isDead) p1.display(hd);
  if (!p2.isDead) p2.display(hd);

  for (HitParticle p : hitParticles) p.display(hd);
  for (DeathParticle p : deathParticles) p.display(hd);

  // Écran final affiché pendant que le survivant rebondit
  if (matchTermine) {
    hd.fill(0, min(endTimer * 2, 150)); 
    hd.rect(0, 0, hdWidth, hdHeight);
    hd.fill(255, min(endTimer * 4, 255));
    hd.textSize(100);
    hd.textAlign(CENTER, CENTER);
    if (!p1.isDead) hd.text(name1 + " WINS!", hdWidth/2, hdHeight/2);
    else hd.text(name2 + " WINS!", hdWidth/2, hdHeight/2);
  }

  hd.popMatrix(); 
  hd.endDraw();
  imageMode(CORNER);
  image(hd, 0, 0, width, height);

  // ================= EXPORT & CLÔTURE =================
  if (exporterFrames) {
    hd.save(workDir + "/frames/frame-" + nf(frameCount, 4) + ".png");
  }
  
  // Clôture automatique après 3 secondes (180 frames)
  if (matchTermine && endTimer >= 180) {
    if (exporterFrames) {
      // Exporte le CSV proprement avec les en-têtes requises
      saveTable(audioLog, workDir + "/audio_log.csv");
      println("Export terminé avec succès !");
    }
    exit();
  }
}

// ============================================================
// PHYSIQUE ET CLASSES
// ============================================================
void updateArenaPhysics() {
  arenaOffsetX *= 0.5;
  arenaOffsetY *= 0.5;
}

void triggerArenaImpact(float forceX, float forceY) {
  arenaOffsetX = forceX;
  arenaOffsetY = forceY;
  arenaBorder = color(255, 255, 255); 
}

void spawnHitEffects(PVector pos, PVector hitNormal) {
  for(int i = 0; i < 35; i++) {
    hitParticles.add(new HitParticle(pos.x, pos.y, hitNormal, color(220, 10, 30), false));
  }
  PVector sparkNormal = hitNormal.copy().mult(-1);
  for(int i = 0; i < 20; i++) {
    hitParticles.add(new HitParticle(pos.x, pos.y, sparkNormal, color(255, 230, 100), true));
  }
}

void gererCollisionJoueurs(Player b1, Player b2) {
  if (b1.isDead || b2.isDead) return;

  PVector d = PVector.sub(b1.pos, b2.pos);
  float dist = d.mag();
  float minD = b1.r + b2.r;
  float hitD1 = b1.hasSaw ? b1.sawRadius : b1.r;
  float hitD2 = b2.hasSaw ? b2.sawRadius : b2.r;
  
  if (dist >= b1.r + b2.r && dist >= hitD1 + b2.r && dist >= hitD2 + b1.r) return;

  boolean hitOcurred = false;
  
  if (b1.hasSaw && dist < b1.sawRadius + b2.r) {
    PVector hitNormal = PVector.sub(b2.pos, b1.pos).normalize();
    b2.takeDamage(hitNormal);
    b1.hasSaw = false;
    hitOcurred = true;
    spawnHitEffects(PVector.lerp(b1.pos, b2.pos, 0.5), hitNormal);
  } else if (b2.hasSaw && dist < b2.sawRadius + b1.r) {
    PVector hitNormal = PVector.sub(b1.pos, b2.pos).normalize();
    b1.takeDamage(hitNormal);
    b2.hasSaw = false;
    hitOcurred = true;
    spawnHitEffects(PVector.lerp(b1.pos, b2.pos, 0.5), hitNormal);
  }

  if (hitOcurred) {
    enregistrerSon("hit");
    arenaBorder = color(255, 50, 50); 
    arenaOffsetX = random(-15, 15);
    arenaOffsetY = random(-15, 15);
    hitStopTimer = 6; 
  } 
  else if (dist < minD) {
    float overlap = minD - dist;
    PVector corr = d.copy().normalize().mult(overlap / 2.0);
    b1.pos.add(corr);
    b2.pos.sub(corr);

    PVector normale = d.copy().normalize();
    PVector relVel  = PVector.sub(b1.vel, b2.vel);
    float dot = relVel.dot(normale);
    if (dot > 0) return;

    float rest = 1.2; 
    float impulse = -(1 + rest) * dot / 2.0;
    b1.vel.add(PVector.mult(normale, impulse));
    b2.vel.sub(PVector.mult(normale, impulse));
    
    b1.vel.rotate(random(-0.06, 0.06));
    b2.vel.rotate(random(-0.06, 0.06));
  }
}

class Player {
  PVector pos, vel;
  float r = 70;
  color col;
  PImage imgMasked;
  int lives = maxLives;
  boolean isDead = false;
  
  boolean hasSaw = false;
  float sawTimer = 0;
  float sawRadius = 110;
  float sawRot = 0;

  float damageFlashTimer = 0;
  float bleedTimer = 0;

  Player(PImage rawImg, color c, float x, float y, float vx, float vy) {
    col = c;
    pos = new PVector(x, y);
    vel = new PVector(vx, vy);
    imgMasked = creerImageCirculaire(rawImg, r);
  }

  void equipSaw() {
    hasSaw = true;
    sawTimer = 300; 
  }

  void takeDamage(PVector hitNormal) {
    lives--;
    damageFlashTimer = 22; 
    bleedTimer = 60; 
    vel.add(PVector.mult(hitNormal, 30)); 
  }

  void die() {
    isDead = true;
    hasSaw = false;
    enregistrerSon("death"); 
    
    for(int i = 0; i < 60; i++) deathParticles.add(new DeathParticle(pos.x, pos.y, color(200, 10, 20))); 
    for(int i = 0; i < 50; i++) deathParticles.add(new DeathParticle(pos.x, pos.y, col)); 
    for(int i = 0; i < 40; i++) deathParticles.add(new DeathParticle(pos.x, pos.y, color(40, 40, 45))); 
  }

  void update() {
    if (isDead) return;

    pos.add(vel);
    
    float speed = vel.mag();
    if (speed < 9) { vel.setMag(9); } 
    else if (speed > 9) { vel.mult(0.95); }

    boolean bounced = false;

    if (pos.x - r < arenaX) { pos.x = arenaX + r; vel.x *= -1; bounced = true; triggerArenaImpact(-8, 0); }
    if (pos.x + r > arenaX + arenaW) { pos.x = arenaX + arenaW - r; vel.x *= -1; bounced = true; triggerArenaImpact(8, 0); }
    if (pos.y - r < arenaY) { pos.y = arenaY + r; vel.y *= -1; bounced = true; triggerArenaImpact(0, -8); }
    if (pos.y + r > arenaY + arenaH) { pos.y = arenaY + arenaH - r; vel.y *= -1; bounced = true; triggerArenaImpact(0, 8); }

    if (bounced) {
      enregistrerSon("bounce"); // Enregistre "bounce" au rebond contre un mur
      vel.mult(1.15); 
      vel.rotate(random(-0.1, 0.1)); 
    }

    if (hasSaw) {
      sawRot += 0.15;
      sawTimer--;
      if (sawTimer <= 0) hasSaw = false; 
    }

    if (damageFlashTimer > 0) damageFlashTimer--;
    if (bleedTimer > 0) {
      bleedTimer--;
      if (frameCount % 4 == 0) {
        PVector dropDir = vel.copy().mult(-1).normalize().rotate(random(-0.5, 0.5));
        hitParticles.add(new HitParticle(pos.x, pos.y, dropDir, color(200, 10, 20), false));
      }
    }
  }

  void display(PGraphics g) {
    if (isDead) return;

    g.pushMatrix();
    g.translate(pos.x, pos.y);
    
    if (hasSaw) {
      g.pushMatrix();
      g.rotate(sawRot);
      dessinerEtoile(g, 0, 0, r + 15, sawRadius, 16, color(200));
      dessinerEtoile(g, 0, 0, r + 5, sawRadius - 8, 16, color(255));
      g.popMatrix();
    }

    g.noStroke();
    g.fill(0, 100);
    g.ellipse(10, 15, r * 2 + 16, r * 2 + 16);
    g.fill(col, 120);
    g.ellipse(0, 0, r * 2 + 30, r * 2 + 30);
    g.fill(col);
    g.ellipse(0, 0, r * 2 + 12, r * 2 + 12);
    g.fill(15, 20, 25);
    g.ellipse(0, 0, r * 2 + 4, r * 2 + 4);
    
    if (imgMasked != null) {
      g.imageMode(CENTER);
      g.image(imgMasked, 0, 0);
    }
    
    g.fill(255, 60);
    g.arc(0, 0, r * 2, r * 2, PI, TWO_PI);
    g.fill(255, 120);
    g.pushMatrix();
    g.rotate(-PI/4);
    g.ellipse(0, -r * 0.7, r * 1.2, r * 0.3);
    g.popMatrix();
    
    g.noFill();
    g.stroke(255, 80);
    g.strokeWeight(3);
    g.ellipse(0, 0, r * 2 - 2, r * 2 - 2);

    if (damageFlashTimer > 0) {
      g.noStroke();
      if (damageFlashTimer > 17) {
        g.fill(255, 200); 
      } else {
        g.fill(255, 0, 0, map(damageFlashTimer, 0, 17, 0, 160)); 
      }
      g.ellipse(0, 0, r * 2 + 12, r * 2 + 12);
    }

    g.popMatrix();
  }
}

class DeathParticle {
  PVector pos, vel;
  float life, maxLife, sz;
  color c;

  DeathParticle(float x, float y, color col) {
    pos = new PVector(x, y);
    float a = random(TWO_PI);
    float s = random(5, 35); 
    vel = new PVector(cos(a)*s, sin(a)*s);
    maxLife = random(60, 150); 
    life = maxLife;
    c = col;
    sz = random(8, 28);
  }

  void update() {
    pos.add(vel);
    vel.mult(0.90); 
    vel.y += 0.3; 
    life--;
  }

  void display(PGraphics g) {
    float alpha = map(life, 0, maxLife, 0, 255);
    g.noStroke();
    g.fill(red(c), green(c), blue(c), alpha);
    float currentSz = sz * (life / maxLife);
    g.ellipse(pos.x, pos.y, currentSz, currentSz);
  }
}

class HitParticle {
  PVector pos, vel;
  float life, maxLife, sz;
  color c;
  boolean isSpark;

  HitParticle(float x, float y, PVector dir, color col, boolean spark) {
    pos = new PVector(x, y);
    float spread = spark ? random(-PI/2, PI/2) : random(-PI/4, PI/4);
    vel = dir.copy().rotate(spread).mult(spark ? random(10, 25) : random(5, 15));
    maxLife = random(15, 35);
    life = maxLife;
    c = col;
    isSpark = spark;
    sz = isSpark ? random(4, 7) : random(6, 16);
  }

  void update() {
    pos.add(vel);
    vel.mult(isSpark ? 0.82 : 0.90);
    life--;
  }

  void display(PGraphics g) {
    g.noStroke();
    float alpha = map(life, 0, maxLife, 0, 255);
    if (isSpark) {
      g.fill(c, alpha);
      g.pushMatrix();
      g.translate(pos.x, pos.y);
      g.rotate(vel.heading());
      g.rectMode(CENTER);
      g.rect(0, 0, sz * 2.5 * (life/maxLife), sz * 0.5);
      g.rectMode(CORNER);
      g.popMatrix();
    } else {
      g.fill(red(c), green(c), blue(c), alpha);
      g.ellipse(pos.x, pos.y, sz * (life/maxLife), sz * (life/maxLife));
    }
  }
}

class Saw {
  PVector pos;
  float r = 40;
  float rot = 0;
  Saw(float x, float y) { pos = new PVector(x, y); }
  void update() { rot += 0.1; }
  boolean checkCollision(PVector pPos, float pR) { return PVector.dist(pos, pPos) < r + pR; }

  void display(PGraphics g, boolean isEquipped) {
    g.pushMatrix();
    g.translate(pos.x, pos.y);
    g.rotate(rot);
    g.fill(0, 80);
    g.noStroke();
    dessinerEtoile(g, 5, 8, r * 0.5, r, 8, color(0, 80));
    dessinerEtoile(g, 0, 0, r * 0.5, r, 8, color(255));
    g.fill(100);
    g.ellipse(0, 0, r*0.4, r*0.4);
    g.fill(255);
    g.ellipse(0, 0, r*0.15, r*0.15);
    g.popMatrix();
  }
}

class Heart {
  PVector pos;
  float r = 35;
  float pulseTime = 0;
  Heart(float x, float y) { pos = new PVector(x, y); }
  void update() { pulseTime += 0.1; }
  boolean checkCollision(PVector pPos, float pR) { return PVector.dist(pos, pPos) < r + pR; }

  void display(PGraphics g) {
    g.pushMatrix();
    g.translate(pos.x, pos.y);
    float currentScale = 1.0 + 0.15 * sin(pulseTime);
    g.scale(currentScale);
    g.pushMatrix();
    g.translate(4, 6);
    dessinerCoeur(g, r, color(0, 80));
    g.popMatrix();
    dessinerCoeur(g, r * 1.3, color(255, 50, 100, 80));
    dessinerCoeur(g, r, color(255, 40, 80));
    dessinerCoeur(g, r * 0.6, color(255, 120, 150));
    g.popMatrix();
  }
}

// ============================================================
// DESSIN UI ET DÉCOR
// ============================================================
void dessinerArene(PGraphics g) {
  g.pushMatrix();
  float centerX = arenaX + arenaW / 2;
  float centerY = arenaY + arenaH / 2;
  g.translate(centerX + arenaOffsetX, centerY + arenaOffsetY);
  
  g.fill(0, 80);
  g.noStroke();
  g.rectMode(CENTER);
  g.rect(15, 20, arenaW, arenaH, 20);
  
  g.noFill();
  g.strokeWeight(12);
  g.stroke(arenaBorder);
  g.rect(0, 0, arenaW, arenaH, 20);
  g.rectMode(CORNER); 
  g.popMatrix();
  
  arenaBorder = lerpColor(arenaBorder, color(80, 80, 220), 0.15);
}

void dessinerUI(PGraphics g) {
  g.textAlign(CENTER, CENTER);
  g.fill(255);
  g.textSize(60);
  g.text(name1, hdWidth/2, 150);
  dessinerBarreVie(g, hdWidth/2, 220, p1.lives, maxLives, color(30, 80, 255));
  
  g.fill(255);
  g.textSize(60);
  g.text(name2, hdWidth/2, 320);
  dessinerBarreVie(g, hdWidth/2, 390, p2.lives, maxLives, color(255, 40, 40));
}

void dessinerBarreVie(PGraphics g, float x, float y, int lives, int max, color c) {
  float rectW = 120;
  float rectH = 30;
  float spacing = 15;
  float totalW = (max * rectW) + ((max - 1) * spacing);
  float startX = x - totalW / 2;
  
  g.stroke(255);
  g.strokeWeight(3);
  for (int i = 0; i < max; i++) {
    if (i < lives) {
      g.fill(c);
      g.rect(startX + i * (rectW + spacing), y - rectH/2, rectW, rectH, 10);
      g.noStroke();
      g.fill(255, 80);
      g.rect(startX + i * (rectW + spacing), y - rectH/2, rectW, rectH/2, 10, 10, 0, 0);
      g.stroke(255); 
    } else {
      g.noFill();
      g.rect(startX + i * (rectW + spacing), y - rectH/2, rectW, rectH, 10);
    }
  }
}

void dessinerEtoile(PGraphics g, float x, float y, float radius1, float radius2, int npoints, color c) {
  float angle = TWO_PI / npoints;
  float halfAngle = angle / 2.0;
  if (alpha(c) < 255) { g.fill(c); } else { g.fill(c); }
  g.noStroke();
  g.beginShape();
  for (float a = 0; a < TWO_PI; a += angle) {
    float sx = x + cos(a) * radius2;
    float sy = y + sin(a) * radius2;
    g.vertex(sx, sy);
    sx = x + cos(a + halfAngle) * radius1;
    sy = y + sin(a + halfAngle) * radius1;
    g.vertex(sx, sy);
  }
  g.endShape(CLOSE);
}

void dessinerCoeur(PGraphics g, float s, color c) {
  g.fill(c);
  g.noStroke();
  g.beginShape();
  g.vertex(0, -s * 0.2);
  g.bezierVertex(s * 0.6, -s * 0.8, s * 1.2, s * 0.2, 0, s * 0.8);
  g.bezierVertex(-s * 1.2, s * 0.2, -s * 0.6, -s * 0.8, 0, -s * 0.2);
  g.endShape(CLOSE);
}

// ============================================================
// UTILITAIRES
// ============================================================
void enregistrerSon(String type) {
  if (!exporterFrames) return; // Sécurité optionnelle
  TableRow row = audioLog.addRow();
  row.setInt("frame", frameCount);
  row.setString("sound", type);
}

String extractName(String filename) {
  String name = filename;
  int lastSlash = max(name.lastIndexOf('/'), name.lastIndexOf('\\'));
  if (lastSlash >= 0) name = name.substring(lastSlash + 1);
  int dotIndex = name.lastIndexOf('.');
  if (dotIndex > 0) name = name.substring(0, dotIndex);
  
  String[] words = name.split(" ");
  String res = "";
  for (String w : words) {
    if (w.length() > 0) { res += w.substring(0, 1).toUpperCase() + w.substring(1).toLowerCase() + " "; }
  }
  return res.trim();
}

PImage creerImageCirculaire(PImage src, float radius) {
  if (src == null) return null;
  int targetD = (int)(radius * 2);
  float ratioW = (float)targetD / src.width;
  float ratioH = (float)targetD / src.height;
  float scaleFactor = max(ratioW, ratioH);
  int newW = (int)(src.width * scaleFactor);
  int newH = (int)(src.height * scaleFactor);
  
  PImage scaled = src.copy();
  scaled.resize(newW, newH);
  
  int cropX = (newW - targetD) / 2;
  int cropY = (newH - targetD) / 2;
  PImage cropped = scaled.get(cropX, cropY, targetD, targetD);
  
  PImage masque = createImage(targetD, targetD, ALPHA);
  masque.loadPixels();
  for (int y = 0; y < targetD; y++) {
    for (int x = 0; x < targetD; x++) {
      if (dist(x, y, radius, radius) <= radius) { masque.pixels[y * targetD + x] = color(255); } 
      else { masque.pixels[y * targetD + x] = color(0); }
    }
  }
  masque.updatePixels();
  cropped.mask(masque);
  return cropped;
}