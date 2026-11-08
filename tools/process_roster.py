"""
Sprite Processor for PowerSlave Exhumed 8-Player Roster.
Extracts, keys out, normalizes, and packages sprites for all 8 players.
"""
import os
import sys
import json
import numpy as np
from PIL import Image
import cv2

SPRITES_DIR = r"S:\_Vibe_Coding\PowerSlave_Exhumed_Together\_Sprites"
BRAIN_DIR = r"C:\Users\vadso\.gemini\antigravity\brain\dd108097-7f9c-4938-8bbf-22202c2dfb0d"

ROSTER = [
    {
        "id": 1,
        "key": "player1_mercenary_commander",
        "folder": "Player1_Mercenary_Commander",
        "name": "Captain John Ramses",
        "role": "Expedition Commander / Assault Commando",
        "desc": "Short buzzcut, olive-drab tactical vest, desert khaki BDUs, assault carbine."
    },
    {
        "id": 2,
        "key": "player2_ruins_scout",
        "folder": "Player2_Ruins_Scout",
        "name": "Dr. Amira Vance",
        "role": "Ruins Scout & Field Archaeologist",
        "desc": "Ivory tagelmust headscarf, dark braid, cream linen shirt, dark teal trousers, bronze ankh, SMG."
    },
    {
        "id": 3,
        "key": "player3_desert_tracker",
        "folder": "Player3_Desert_Tracker",
        "name": "Tariq Al-Mansur",
        "role": "Desert Ranger & Survival Tracker",
        "desc": "Lapis-blue keffiyeh scarf, bronze scarab chest emblem, sand fatigues, dust goggles, carbine."
    },
    {
        "id": 4,
        "key": "player4_heavy_gunner",
        "folder": "Player4_Heavy_Gunner",
        "name": "Sgt. Marcus 'Sarge' Stone",
        "role": "Heavy Weapons Specialist & Breacher",
        "desc": "Muscular build, sleeveless tiger-stripe vest, red bandana, ammo bandoliers, heavy combat rifle."
    },
    {
        "id": 5,
        "key": "player5_tactical_recon",
        "folder": "Player5_Tactical_Recon",
        "name": "Lt. Elena Rostova",
        "role": "Tactical Recon & Electronic Comms",
        "desc": "Cropped blonde hair, charcoal-slate infiltration suit, comms headset with antenna, bullpup rifle."
    },
    {
        "id": 6,
        "key": "player6_occult_scholar",
        "folder": "Player6_Occult_Scholar",
        "name": "Prof. Victor Sterling",
        "role": "Occult Historian & Antiquarian",
        "desc": "Trimmed gray beard, wire spectacles, safari bush jacket, binoculars, scoped battle rifle."
    },
    {
        "id": 7,
        "key": "player7_tomb_sapper",
        "folder": "Player7_Tomb_Sapper",
        "name": "Hassan Al-Khatib",
        "role": "Tomb Sapper & Subterranean Engineer",
        "desc": "Miner helmet with brass carbide lamp, industrial tan coveralls, demolition pouches, trench shotgun."
    },
    {
        "id": 8,
        "key": "player8_desert_legionnaire",
        "folder": "Player8_Desert_Legionnaire",
        "name": "Capitaine Gabriel Cruz",
        "role": "Foreign Legionnaire Desert Mercenary",
        "desc": "White desert kepi with havelock neck cloth, handlebar mustache, cross bandoliers, battle rifle."
    }
]

def keyout_background(img_path):
    """Cleanly key out background using corner color sampling and flood fill / color distance."""
    im = Image.open(img_path).convert("RGB")
    arr = np.array(im)
    h, w, _ = arr.shape
    
    # Sample corner 15x15 pixels to find background color
    corners = np.concatenate([
        arr[:15, :15].reshape(-1, 3),
        arr[:15, -15:].reshape(-1, 3),
        arr[-15:, :15].reshape(-1, 3),
        arr[-15:, -15:].reshape(-1, 3)
    ], axis=0)
    bg_median = np.median(corners, axis=0)
    
    # Calculate color distance in RGB space
    diff = np.sqrt(np.sum((arr.astype(float) - bg_median)**2, axis=2))
    
    # Threshold for background
    mask = (diff > 25).astype(np.uint8) * 255
    
    # Morphological cleaning: remove tiny noise specks in background
    kernel = np.ones((3, 3), np.uint8)
    mask = cv2.morphologyEx(mask, cv2.MORPH_OPEN, kernel, iterations=1)
    
    # Create RGBA
    rgba = np.dstack([arr, mask])
    return Image.fromarray(rgba)

print("Sprite processor helper ready.")
