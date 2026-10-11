import os
import json
import time
import requests
import subprocess
from datetime import datetime

API_BASE = "https://autonimation-studio.com/api"
PROJECTS_DIR = "animations"

def get_local_projects():
    projects = []
    if os.path.exists(PROJECTS_DIR):
        for folder in os.listdir(PROJECTS_DIR):
            config_path = os.path.join(PROJECTS_DIR, folder, 'config.json')
            if os.path.exists(config_path):
                with open(config_path, 'r', encoding='utf-8') as f:
                    try:
                        config = json.load(f)
                        if config.get("enabled", True):
                            step_names = [step.get("name", "") for step in config.get("steps", [])]
                            description = " Workflow : " + " ➔ ".join(step_names)
                            projects.append({
                                "id": folder,
                                "name": config.get("name", folder).replace("_", " ").title(),
                                "description": description
                            })
                    except json.JSONDecodeError:
                        print(f"[!] Erreur de lecture du config.json dans {folder}")
    return projects

def register_to_vps():
    projects = get_local_projects()
    print(f"[*] Registering {len(projects)} project(s) to VPS...")
    try:
        requests.post(f"{API_BASE}/engine/register", json={"projects": projects})
    except Exception as e:
        pass

def update_status(job_id, status):
    print(f"[>] Updating job {job_id} status to: {status}")
    try:
        requests.post(f"{API_BASE}/engine/jobs/update", json={"job_id": job_id, "status": status})
    except:
        pass

def create_job_context(project_id, original_dir):
    """
    Lit le calendrier et crée un contexte job.json respectant la spécification.
    """
    matches_path = os.path.join(original_dir, "data", "matches.json")
    logos_dir = os.path.join(original_dir, "assets", "logos")
    temp_dir = os.path.join(original_dir, "output", "temporary")
    out_dir = os.path.join(original_dir, "output", "videos")
    
    with open(matches_path, 'r', encoding='utf-8') as f:
        matches = json.load(f)
        
    if not matches:
        raise Exception("Le calendrier data/matches.json est vide.")
        
    # Option B : On prend le premier match de ton calendrier
    match = matches[0]
    home = match.get("home", "Home")
    away = match.get("away", "Away")
    date = match.get("date", datetime.now().strftime("%Y-%m-%d"))
    
    # Création d'un ID de job unique
    real_job_id = f"{date}_{home.replace(' ', '_')}_{away.replace(' ', '_')}_{project_id}"
    
    # Création des dossiers de sortie si inexistants
    work_dir = os.path.join(temp_dir, real_job_id)
    os.makedirs(work_dir, exist_ok=True)
    os.makedirs(out_dir, exist_ok=True)
    
    # Construction du JSON respectant exactement ta spécification
    job_data = {
        "job_id": real_job_id,
        "match": match,
        "resources": {
            "home_logo": os.path.join(logos_dir, f"{home}.png"),
            "away_logo": os.path.join(logos_dir, f"{away}.png")
        },
        "work_dir": work_dir,
        "output_video": os.path.join(out_dir, f"{real_job_id}.mp4")
    }
    
    # Écriture du fichier job temporaire à la racine
    job_file_path = os.path.join(original_dir, f"{real_job_id}.json")
    with open(job_file_path, "w", encoding="utf-8") as f:
        json.dump(job_data, f, indent=4)
        
    return job_file_path, real_job_id

def poll_jobs():
    print("[*] Engine listening for new jobs...")
    original_dir = os.getcwd() # Sauvegarde de la racine du projet
    
    while True:
        try:
            res = requests.get(f"{API_BASE}/engine/jobs/pending")
            if res.status_code == 200:
                data = res.json()
                if data.get("status") == "PENDING":
                    web_job_id = data["id"]
                    project_id = data["project_id"]
                    print(f"\n[!] New job received from Web: {web_job_id} for project {project_id}")
                    
                    try:
                        # --- ÉTAPE 1 : GENERATION (Lecture Calendrier) ---
                        update_status(web_job_id, "RUNNING")
                        print("[*] Reading data/matches.json and generating job context...")
                        job_file_path, real_job_id = create_job_context(project_id, original_dir)
                        print(f"[*] Context generated for match: {real_job_id}")
                        time.sleep(1)
                        
                        # --- ÉTAPE 2 : EXECUTION DYNAMIQUE DU PIPELINE ---
                        update_status(web_job_id, "RENDERING")
                        
                        project_path = os.path.join(original_dir, PROJECTS_DIR, project_id)
                        config_path = os.path.join(project_path, 'config.json')
                        
                        # Lecture des étapes configurées dans ton vrai config.json
                        with open(config_path, 'r', encoding='utf-8') as f:
                            anim_config = json.load(f)
                            
                        steps = anim_config.get("steps", [])
                        
                        for i, step in enumerate(steps):
                            step_type = step.get("type")
                            step_name = step.get("name", f"Step {i+1}")
                            
                            print(f"\n[>>] Executing {step_type.upper()} : {step_name}")
                            
                            if step_type == "processing":
                                sketch_dir = os.path.join(project_path, step.get("project"))
                                # Commande officielle pour lancer Processing en ligne de commande
                                cmd = ["processing-java", "--force", f"--sketch={sketch_dir}", "--run", job_file_path]
                                subprocess.run(cmd, check=True)
                                
                            elif step_type == "python":
                                script_file = os.path.join(project_path, step.get("script"))
                                cmd = ["python", script_file, job_file_path]
                                subprocess.run(cmd, check=True)
                        
                        print("\n[*] Rendering pipeline successful!")
                        update_status(web_job_id, "READY")
                        print(f"[*] Job {web_job_id} completed. MP4 available in output/videos/")
                            
                    except subprocess.CalledProcessError as e:
                        print(f"\n[!] Error during execution of the pipeline: {e}")
                        update_status(web_job_id, "FAILED")
                    except Exception as e:
                        print(f"\n[!] Unexpected error: {e}")
                        update_status(web_job_id, "FAILED")
                    finally:
                        os.chdir(original_dir)
                        
        except Exception as e:
            pass
        
        time.sleep(3)

if __name__ == "__main__":
    register_to_vps()
    poll_jobs()