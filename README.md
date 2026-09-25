# Football Video Automation (V1)

Moteur d'automatisation de génération et publication de vidéos de football.

## Objectif V1
À partir d'un calendrier de matchs (JSON), détecter les matchs du lendemain, choisir une animation, générer automatiquement une vidéo et préparer sa publication sur un compte TikTok dans une fenêtre horaire configurable.

## Architecture
- **Moteur (`app/`)** : Capacités génériques d'orchestration.
- **Animations (`animations/`)** : Pipelines de rendus indépendants (Python, Processing, etc.).
- **Plateformes (`platforms/`)** : Modules de communication externes (ex: TikTok).# football_video_automation
