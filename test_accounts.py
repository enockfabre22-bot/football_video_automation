from app.publication_manager import PublicationManager

def test_loading():
    print("--- TEST CHARGEMENT DES COMPTES ---")
    manager = PublicationManager()
    
    # 1. On charge les comptes
    accounts = manager.get_all_active_accounts("accounts")
    
    print(f"\n✅ {len(accounts)} compte(s) actif(s) trouvé(s) :")
    for acc in accounts:
        print(f" - {acc['internal_id']} ({acc.get('platform', 'inconnue')})")
        print(f"   Stratégies : {[slot['id'] for slot in acc.get('schedule_slots', [])]}")

if __name__ == "__main__":
    test_loading()