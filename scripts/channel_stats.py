"""
Statistiques détaillées des dernières vidéos d'une chaîne YouTube (lecture seule).

    python scripts/channel_stats.py youtube_avantfoot [--max 60] [--no-retention]

Pré-requis : python scripts/youtube_stats_auth.py <compte> (jeton accounts/<compte>/stats_token.json).
Par vidéo : date (heure de Paris), durée, vues, vues engagées, durée moyenne regardée, % moyen regardé,
likes / commentaires / partages / abonnés gagnés, vues par source de trafic (fil Shorts, recherche,
suggestions...) et courbe de rétention. Tableau affiché + JSON complet dans output/stats/.
Les données YouTube Analytics ont 1 à 3 jours de retard : les vidéos très récentes y sont incomplètes.
"""
import argparse
import json
import re
import sys
from datetime import datetime, timezone
from pathlib import Path
from zoneinfo import ZoneInfo

from google.auth.transport.requests import Request
from google.oauth2.credentials import Credentials
from googleapiclient.discovery import build
from googleapiclient.errors import HttpError

BASE_DIR = Path(__file__).resolve().parent.parent
PARIS = ZoneInfo("Europe/Paris")
SOURCES = {"SHORTS": "shorts", "YT_SEARCH": "search", "RELATED_VIDEO": "suggest", "BROWSE": "browse",
           "YT_CHANNEL": "channel", "EXT_URL": "external", "NOTIFICATION": "notif", "SUBSCRIBER": "subs",
           "PLAYLIST": "playlist", "NO_LINK_OTHER": "direct", "HASHTAGS": "hashtags", "END_SCREEN": "endscreen"}


def credentials(account):
    path = BASE_DIR / "accounts" / account / "stats_token.json"
    if not path.exists():
        sys.exit(f"❌ {path} introuvable : lance d'abord python scripts/youtube_stats_auth.py {account}")
    info = json.loads(path.read_text(encoding="utf-8"))
    creds = Credentials.from_authorized_user_info(info, info.get("scopes"))
    if not creds.valid:
        creds.refresh(Request())
        path.write_text(creds.to_json(), encoding="utf-8")
    return creds


def iso_seconds(d):
    m = re.match(r"P(?:(\d+)D)?T?(?:(\d+)H)?(?:(\d+)M)?(?:(\d+)S)?", d or "")
    if not m:
        return 0
    dd, h, mi, s = (int(x or 0) for x in m.groups())
    return dd * 86400 + h * 3600 + mi * 60 + s


def list_videos(yt, max_videos):
    ch = yt.channels().list(part="snippet,contentDetails,statistics", mine=True).execute()["items"][0]
    uploads = ch["contentDetails"]["relatedPlaylists"]["uploads"]
    ids, token = [], None
    while len(ids) < max_videos:
        r = yt.playlistItems().list(part="contentDetails", playlistId=uploads, maxResults=50, pageToken=token).execute()
        ids += [it["contentDetails"]["videoId"] for it in r["items"]]
        token = r.get("nextPageToken")
        if not token:
            break
    ids = ids[:max_videos]
    videos = []
    for i in range(0, len(ids), 50):
        r = yt.videos().list(part="snippet,contentDetails,statistics,status", id=",".join(ids[i:i + 50])).execute()
        for v in r["items"]:
            pub = datetime.fromisoformat(v["snippet"]["publishedAt"].replace("Z", "+00:00"))
            st = v.get("statistics", {})
            videos.append({
                "id": v["id"], "title": v["snippet"]["title"],
                "published": pub.astimezone(PARIS).strftime("%Y-%m-%d %H:%M"),
                "age_h": round((datetime.now(timezone.utc) - pub).total_seconds() / 3600, 1),
                "duration_s": iso_seconds(v["contentDetails"]["duration"]),
                "privacy": v.get("status", {}).get("privacyStatus"),
                "views": int(st.get("viewCount", 0)), "likes": int(st.get("likeCount", 0)),
                "comments": int(st.get("commentCount", 0)),
            })
    return ch, videos


def query(yta, **kw):
    r = yta.reports().query(ids="channel==MINE", **kw).execute()
    cols = [h["name"] for h in r.get("columnHeaders", [])]
    return [dict(zip(cols, row)) for row in r.get("rows", [])]


def analytics(yta, videos, start, end, retention):
    by_id = {v["id"]: v for v in videos}
    filt = "video==" + ",".join(by_id)
    base = "views,estimatedMinutesWatched,averageViewDuration,averageViewPercentage,likes,comments,shares,subscribersGained"
    rows = None
    for metrics in ("engagedViews," + base, base):        # engagedViews : métrique récente, absente de certains rapports
        try:
            rows = query(yta, startDate=start, endDate=end, metrics=metrics, dimensions="video", filters=filt,
                         maxResults=200, sort="-views")
            break
        except HttpError as e:
            last = e
    if rows is None:
        raise last
    for row in rows:
        v = by_id.get(row.pop("video"))
        if v:
            v["analytics"] = row

    for v in videos:
        try:
            src = query(yta, startDate=start, endDate=end, metrics="views", dimensions="insightTrafficSourceType",
                        filters=f"video=={v['id']}")
            v["traffic"] = {SOURCES.get(r["insightTrafficSourceType"], r["insightTrafficSourceType"].lower()): r["views"]
                            for r in src}
        except HttpError as e:
            v["traffic_error"] = str(e)[:200]
        if retention:
            try:
                ret = query(yta, startDate=start, endDate=end, metrics="audienceWatchRatio,relativeRetentionPerformance",
                            dimensions="elapsedVideoTimeRatio", filters=f"video=={v['id']}")
                v["retention"] = [[r["elapsedVideoTimeRatio"], round(r["audienceWatchRatio"], 3),
                                   round(r["relativeRetentionPerformance"], 3)] for r in ret]
            except HttpError as e:
                v["retention_error"] = str(e)[:200]


def retention_at(v, ratio):
    pts = v.get("retention") or []
    best = min(pts, key=lambda p: abs(p[0] - ratio), default=None)
    return best[1] if best else None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("account")
    ap.add_argument("--max", type=int, default=60, help="nombre de vidéos récentes analysées")
    ap.add_argument("--no-retention", action="store_true")
    a = ap.parse_args()
    sys.stdout.reconfigure(encoding="utf-8")

    creds = credentials(a.account)
    yt = build("youtube", "v3", credentials=creds, cache_discovery=False)
    yta = build("youtubeAnalytics", "v2", credentials=creds, cache_discovery=False)
    ch, videos = list_videos(yt, a.max)
    start = min(v["published"][:10] for v in videos) if videos else "2020-01-01"
    end = datetime.now(PARIS).strftime("%Y-%m-%d")
    try:
        analytics(yta, videos, start, end, not a.no_retention)
    except HttpError as e:
        print(f"⚠️ YouTube Analytics indisponible ({e.status_code}) : {str(e)[:300]}")
        print("   -> active « YouTube Analytics API » dans le projet Google Cloud, puis relance.")

    st = ch["statistics"]
    print(f"\n{ch['snippet']['title']} : {st.get('subscriberCount')} abonnés, {st.get('videoCount')} vidéos, "
          f"{st.get('viewCount')} vues au total  (analytics {start} -> {end})\n")
    print(f"{'publiée (Paris)':16} {'durée':>5} {'vues':>6} {'engag':>6} {'%vu':>5} {'moy s':>5} {'like':>4} "
          f"{'part':>4} {'abo':>3} {'shorts':>6} {'ret25%':>6} {'ret75%':>6}  titre")
    for v in videos:
        an = v.get("analytics", {})
        tr = v.get("traffic", {})
        r25, r75 = retention_at(v, 0.25), retention_at(v, 0.75)
        print(f"{v['published']:16} {v['duration_s']:>5} {v['views']:>6} {an.get('engagedViews', ''):>6} "
              f"{an.get('averageViewPercentage', 0):>5.0f} {an.get('averageViewDuration', 0):>5} {v['likes']:>4} "
              f"{an.get('shares', ''):>4} {an.get('subscribersGained', ''):>3} {tr.get('shorts', 0):>6} "
              f"{'' if r25 is None else f'{r25:.2f}':>6} {'' if r75 is None else f'{r75:.2f}':>6}  {v['title'][:60]}")

    out_dir = BASE_DIR / "output" / "stats"
    out_dir.mkdir(parents=True, exist_ok=True)
    out = out_dir / f"{a.account}_{datetime.now():%Y%m%d_%H%M}.json"
    out.write_text(json.dumps({"channel": {"title": ch["snippet"]["title"], **st}, "start": start, "end": end,
                               "videos": videos}, indent=1, ensure_ascii=False), encoding="utf-8")
    print(f"\nDétail complet (sources de trafic, courbes de rétention) : {out.relative_to(BASE_DIR)}")


if __name__ == "__main__":
    main()
