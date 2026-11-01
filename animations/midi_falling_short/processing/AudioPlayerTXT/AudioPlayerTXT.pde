import javax.sound.midi.*;
import javax.sound.sampled.*;
import java.io.File;
import java.util.ArrayList;
import processing.data.JSONObject;

// ==========================================================
// CONFIGURATION SERVEUR (HEADLESS)
// ==========================================================
String NOTES_FILE;
String workDir = "";
int FPS = 60;          
int INSTRUMENT = 0;   // 0 = Piano

ArrayList<NoteEvent> events = new ArrayList<NoteEvent>();

class NoteEvent {
  long frame;
  int note;
  NoteEvent(long f, int n) {
    this.frame = f;
    this.note = n;
  }
}

void setup() {
  // Règle Processing
  size(100, 100);
  
  // 1. LECTURE DU JOB.JSON (Cerveau Python)
  String jsonPath = (args != null && args.length > 0) ? args[0] : sketchPath("job.json");
  File file = new File(jsonPath);

  if (file.exists()) {
    try {
      JSONObject job = loadJSONObject(file);
      if (!job.isNull("work_dir")) {
        workDir = job.getString("work_dir");
      } else {
        workDir = file.getParent(); 
      }
    } catch(Exception e) {
      println("❌ ERREUR JSON : " + e.getMessage());
      workDir = sketchPath();
    }
  } else {
    workDir = sketchPath();
  }

  // 2. INITIALISATION DES CHEMINS DYNAMIQUES
  NOTES_FILE = workDir + "/notes_log.txt";

  // 3. EXÉCUTION
  loadNotesFile();
  exporterAudioDepuisMidi();
  
  // 4. FERMETURE IMMÉDIATE POUR PASSER AU MIXAGE (Étape 3)
  exit();
}

void draw() {
  // Vide, car le programme tourne en arrière-plan sans interface
}

void loadNotesFile() {
  File f = new File(NOTES_FILE);
  if (!f.exists()) {
    println("❌ ERREUR : Fichier " + NOTES_FILE + " introuvable.");
    return;
  }

  String[] lines = loadStrings(NOTES_FILE);
  if (lines != null) {
    for (String l : lines) {
      if (l.trim().length() > 0) {
        String[] parts = split(l, ',');
        if (parts.length == 2) {
          long frame = Long.parseLong(parts[0].trim());
          int n = Integer.parseInt(parts[1].trim());
          events.add(new NoteEvent(frame, n));
        }
      }
    }
    println("✅ Fichier chargé : " + events.size() + " notes trouvées.");
  }
}

void exporterAudioDepuisMidi() {
  if (events.isEmpty()) return;
  
  try {
    println("Génération du rendu audio...");
    
    Sequence sequence = new Sequence(Sequence.PPQ, 1000);
    Track track = sequence.createTrack();

    MetaMessage tempoMsg = new MetaMessage();
    byte[] tempoBytes = { (byte)0x0F, (byte)0x42, (byte)0x40 };
    tempoMsg.setMessage(0x51, tempoBytes, 3);
    track.add(new MidiEvent(tempoMsg, 0));

    ShortMessage instMsg = new ShortMessage();
    instMsg.setMessage(ShortMessage.PROGRAM_CHANGE, 0, INSTRUMENT, 0);
    track.add(new MidiEvent(instMsg, 0));

    long lastTick = 0;

    for (NoteEvent e : events) {
      long tickStart = Math.round((e.frame / (double) FPS) * 1000.0);
      long tickEnd = tickStart + 800; 
      
      ShortMessage noteOn = new ShortMessage();
      noteOn.setMessage(ShortMessage.NOTE_ON, 0, e.note, 115);
      track.add(new MidiEvent(noteOn, tickStart));

      ShortMessage noteOff = new ShortMessage();
      noteOff.setMessage(ShortMessage.NOTE_OFF, 0, e.note, 0);
      track.add(new MidiEvent(noteOff, tickEnd));
      
      if (tickEnd > lastTick) lastTick = tickEnd;
    }

    ShortMessage endSilence = new ShortMessage();
    endSilence.setMessage(ShortMessage.NOTE_OFF, 0, 60, 0);
    track.add(new MidiEvent(endSilence, lastTick + 2000));

    File tempMidi = new File(workDir + "/temp_render.mid");
    File wavFile = new File(workDir + "/audio.wav");
    
    MidiSystem.write(sequence, 1, tempMidi);

    // Tentative de conversion directe (dépend de Java Sound)
    AudioInputStream ais = AudioSystem.getAudioInputStream(tempMidi);
    AudioSystem.write(ais, AudioFileFormat.Type.WAVE, wavFile);
    ais.close();
    tempMidi.delete(); 

    println(">>> SUCCÈS : Fichier 'audio.wav' généré dans le dossier temporaire !");

  } catch (Exception e) {
    println("❌ Erreur lors de l'export audio : " + e.getMessage());
    e.printStackTrace();
  }
}