"""
Full Sprite Generation & Packaging Pipeline for PowerSlave Exhumed 8-Player Co-Op.
Processes all 8 player characters:
- Keys out backgrounds cleanly
- Extracts individual animation frames
- Creates game-ready 1024px atlases matching KEX engine specifications (height ~98px)
- Generates sprite.txt and anim.txt
- Generates character cards and 00_ROSTER_SHOWCASE.png
- Copies classic John reference files
"""
import os
import sys
import shutil
import json
import numpy as np
from PIL import Image, ImageDraw, ImageFont
import cv2

BRAIN = r"C:\Users\vadso\.gemini\antigravity\brain\dd108097-7f9c-4938-8bbf-22202c2dfb0d"
OUT_ROOT = r"S:\_Vibe_Coding\PowerSlave_Exhumed_Together\_Sprites"
SRC_ART = r"S:\_Vibe_Coding\PowerSlave_Exhumed_Together\PowerSlave_EX_-_CO-OP_Edition-source-code\art"

CHARACTERS = [
    {
        "slot": 1,
        "name": "Captain John Ramses",
        "codename": "Commander",
        "folder": "Player1_Mercenary_Commander",
        "prefix": "player1",
        "image_file": "player1_mercenary_commander_1793820647242.jpg",
        "role": "Expedition Leader / Assault Commando",
        "bio": "Veteran 1990s Special Forces operative sent to investigate the extraterrestrial invasion in Karnak. All-around frontline combat specialist.",
        "gear": "Olive-drab tactical armor vest, desert khaki BDUs, black combat boots, assault carbine.",
        "palette_theme": "Olive Drab & Khaki Camo"
    },
    {
        "slot": 2,
        "name": "Dr. Amira Vance",
        "codename": "Scout",
        "folder": "Player2_Ruins_Scout",
        "prefix": "player2",
        "image_file": "player2_ruins_scout_1793820640509.jpg",
        "role": "Ruins Scout & Field Archaeologist",
        "bio": "Expert on Karnak antiquities and tomb architecture. Agile point-scout capable of navigating collapsed labyrinth corridors.",
        "gear": "Ivory tagelmust headscarf, dark braid, cream linen shirt, dark teal utility trousers, bronze ankh talisman, compact SMG.",
        "palette_theme": "Desert Ivory & Dark Teal"
    },
    {
        "slot": 3,
        "name": "Tariq Al-Mansur",
        "codename": "Tracker",
        "folder": "Player3_Desert_Tracker",
        "prefix": "player3",
        "image_file": "player3_desert_tracker_1793820644415.jpg",
        "role": "Desert Ranger & Survival Tracker",
        "bio": "Local desert guide with unmatched knowledge of valley dunes and subterranean crypt entrances. Relentless tracker and marksman.",
        "gear": "Lapis-blue keffiyeh scarf, bronze scarab chest emblem, sand fatigues, dust goggles around neck, tactical carbine.",
        "palette_theme": "Sand Ochre & Lapis Blue"
    },
    {
        "slot": 4,
        "name": "Sgt. Marcus 'Sarge' Stone",
        "codename": "Breacher",
        "folder": "Player4_Heavy_Gunner",
        "prefix": "player4",
        "image_file": "player4_heavy_gunner_1793820642402.jpg",
        "role": "Heavy Demolitions & Support Gunner",
        "bio": "Massive frontline bruiser and demolitions expert. Carries sustained suppressive firepower into close-quarters catacomb corridors.",
        "gear": "Muscular frame, sleeveless tiger-stripe tactical vest, crimson combat bandana, heavy ammo bandoliers, heavy combat rifle.",
        "palette_theme": "Tiger Camo & Crimson Red"
    },
    {
        "slot": 5,
        "name": "Lt. Elena Rostova",
        "codename": "Recon",
        "folder": "Player5_Tactical_Recon",
        "prefix": "player5",
        "image_file": "player5_tactical_recon_1793820648283.jpg",
        "role": "Covert Ops & Tactical Comms",
        "bio": "Specialized infiltration and communications officer maintaining satellite uplink and scanning alien telemetry in the field.",
        "gear": "Charcoal-slate tactical suit, high collar, comms headset with antenna pack, tactical drop holster, bullpup rifle.",
        "palette_theme": "Charcoal Slate & Cyber Silver"
    },
    {
        "slot": 6,
        "name": "Prof. Victor Sterling",
        "codename": "Scholar",
        "folder": "Player6_Occult_Scholar",
        "prefix": "player6",
        "image_file": "player6_occult_scholar_1793820651049.jpg",
        "role": "Occult Historian & Antiquarian",
        "bio": "Eminent Egyptologist and scholar who deciphered ancient hieroglyphs predicting the Kilmaat arrival. Armed field veteran.",
        "gear": "Safari bush jacket with leather patches, wire spectacles, trimmed grey beard, binoculars strap, scoped hunting rifle.",
        "palette_theme": "British Safari Tan & Leather Brown"
    },
    {
        "slot": 7,
        "name": "Hassan Al-Khatib",
        "codename": "Sapper",
        "folder": "Player7_Tomb_Sapper",
        "prefix": "player7",
        "image_file": "player7_tomb_sapper_1793820650724.jpg",
        "role": "Tomb Engineer & Subterranean Sapper",
        "bio": "Mining engineer recruited to breach sealed stone burial chambers and disarm ancient temple collapse traps.",
        "gear": "Miner helmet with brass carbide headlamp, heavy tan work coveralls, industrial harness, trench combat shotgun.",
        "palette_theme": "Industrial Khaki & Brass Amber"
    },
    {
        "slot": 8,
        "name": "Capitaine Gabriel Cruz",
        "codename": "Legionnaire",
        "folder": "Player8_Desert_Legionnaire",
        "prefix": "player8",
        "image_file": "player8_desert_legionnaire_1793820688898.jpg",
        "role": "Foreign Legion Desert Mercenary",
        "bio": "Cynical veteran of harsh desert campaigns across North Africa. Hardened by decades of desert warfare.",
        "gear": "Desert kepi with white havelock neck cloth, handlebar mustache, khaki combat tunic with shoulder epaulets, crossed bandoliers, battle rifle.",
        "palette_theme": "Legion Khaki & Pure White"
    }
]

def clean_keyout(img_path):
    """Cleanly key out neutral gray background using adaptive border sampling."""
    im = Image.open(img_path).convert("RGB")
    arr = np.array(im)
    
    # Sample 15-pixel border on all 4 sides
    border_pixels = np.concatenate([
        arr[:15, :].reshape(-1, 3),
        arr[-15:, :].reshape(-1, 3),
        arr[:, :15].reshape(-1, 3),
        arr[:, -15:].reshape(-1, 3)
    ], axis=0)
    bg = np.median(border_pixels, axis=0)
    
    diff = np.sqrt(np.sum((arr.astype(float) - bg)**2, axis=2))
    mask = (diff > 24).astype(np.uint8) * 255
    
    # Morphological cleaning to eliminate tiny background noise specks
    kernel = np.ones((3, 3), np.uint8)
    mask = cv2.morphologyEx(mask, cv2.MORPH_OPEN, kernel, iterations=1)
    
    # Close tiny holes inside figures
    mask = cv2.morphologyEx(mask, cv2.MORPH_CLOSE, kernel, iterations=1)
    
    rgba = np.dstack([arr, mask])
    return Image.fromarray(rgba), mask, arr

def find_figures(mask):
    """Detect and sort figures in reading order (rows then columns)."""
    num_labels, labels, stats, centroids = cv2.connectedComponentsWithStats((mask > 0).astype(np.uint8))
    figures = []
    for s in stats[1:]:
        x, y, w, h, area = s
        if area > 450 and w > 14 and h > 30 and w < 800 and h < 800:
            figures.append((int(x), int(y), int(w), int(h), int(area)))
            
    # Group into rows with tolerance of 65px
    figures.sort(key=lambda f: f[1])
    rows = []
    for f in figures:
        placed = False
        for r in rows:
            if abs(f[1] - r[0][1]) < 65:
                r.append(f)
                placed = True
                break
        if not placed:
            rows.append([f])
            
    # Sort each row by X position
    for r in rows:
        r.sort(key=lambda f: f[0])
        
    return rows

def process_all():
    os.makedirs(OUT_ROOT, exist_ok=True)
    
    # Step 1: Copy classic John reference
    classic_dst = os.path.join(OUT_ROOT, "Player1_Classic_John_Reference")
    os.makedirs(classic_dst, exist_ok=True)
    john_files = [
        (os.path.join(SRC_ART, "mod", "sprites", "player", "john.png"), "john.png"),
        (os.path.join(SRC_ART, "mod", "sprites", "player", "john_sprite.txt"), "john_sprite.txt"),
        (os.path.join(SRC_ART, "mod", "sprites", "player", "john_anim.txt"), "john_anim.txt"),
        (os.path.join(SRC_ART, "john_sheet.png"), "john_sheet.png")
    ]
    for src, dst_name in john_files:
        if os.path.exists(src):
            shutil.copy2(src, os.path.join(classic_dst, dst_name))
    print("Classic John reference copied.")
    
    roster_showcase_data = []

    for char in CHARACTERS:
        slot = char["slot"]
        folder = os.path.join(OUT_ROOT, char["folder"])
        frames_dir = os.path.join(folder, "frames")
        os.makedirs(frames_dir, exist_ok=True)
        
        img_path = os.path.join(BRAIN, char["image_file"])
        rgba_sheet, mask, rgb_arr = clean_keyout(img_path)
        
        # Save master sheet
        master_sheet_path = os.path.join(folder, "master_sheet.png")
        rgba_sheet.save(master_sheet_path)
        
        # Detect rows of figures
        rows = find_figures(mask)
        all_figs = [f for r in rows for f in r]
        
        # Measure standing heights (figures with height > 160)
        standing_heights = [f[3] for f in all_figs if f[3] > 160]
        ref_h = np.median(standing_heights) if standing_heights else 220.0
        
        # Target game height is 98px
        scale_factor = 98.0 / ref_h
        
        extracted_frames = []
        
        for idx, (x, y, w, h, area) in enumerate(all_figs):
            crop = rgba_sheet.crop((x, y, x + w, y + h))
            
            # Label based on row and column
            # Find row index
            row_idx = 0
            for ri, r in enumerate(rows):
                if any(f[0] == x and f[1] == y for f in r):
                    row_idx = ri
                    break
            col_idx = [f[0] for f in rows[row_idx]].index(x)
            
            # Determine pose type
            num_rows = len(rows)
            if row_idx == 0:
                pose_type = f"idle_{col_idx+1}"
            elif row_idx == 1:
                pose_type = f"walk_{col_idx+1}"
            elif row_idx == 2:
                pose_type = f"action_{col_idx+1}"
            elif row_idx >= 3 and row_idx < num_rows - 1:
                pose_type = f"combat_{row_idx}_{col_idx+1}"
            else:
                pose_type = f"death_{col_idx+1}"
                
            frame_filename = f"{idx+1:02d}_{pose_type}.png"
            crop.save(os.path.join(frames_dir, frame_filename))
            extracted_frames.append({
                "index": idx,
                "crop": crop,
                "pose": pose_type,
                "orig_box": (x, y, w, h),
                "row": row_idx,
                "col": col_idx
            })
            
        print(f"Player {slot} ({char['name']}): {len(extracted_frames)} frames extracted. Scale factor: {scale_factor:.4f}")
        
        # Pick 4 representative poses for character card and showcase:
        # 1. Front Idle (first row, col 0 or 1)
        # 2. Side / Angle (first row, middle)
        # 3. Action / Shoot (middle row)
        # 4. Death (last row)
        idle_front = extracted_frames[0]["crop"]
        side_angle = extracted_frames[min(2, len(rows[0])-1)]["crop"]
        action_pose = extracted_frames[min(len(rows[0]) + len(rows[1]) if len(rows) > 2 else 0, len(extracted_frames)-1)]["crop"]
        death_pose = extracted_frames[-1]["crop"]
        
        roster_showcase_data.append({
            "slot": slot,
            "char": char,
            "idle_front": idle_front,
            "side_angle": side_angle,
            "action": action_pose,
            "death": death_pose
        })
        
        # Build in-game atlas scaled to 98px
        ATLAS_W = 1024
        scaled_crops = []
        for f in extracted_frames:
            cr = f["crop"]
            nw = max(16, int(round(cr.width * scale_factor)))
            nh = max(16, int(round(cr.height * scale_factor)))
            scaled_im = cr.resize((nw, nh), Image.LANCZOS)
            
            # Calculate anchor (feet center)
            a = np.array(scaled_im)
            fg = a[..., 3] > 128
            h_im = fg.shape[0]
            feet = fg[int(h_im * 0.8):]
            xs = np.where(feet.any(axis=0))[0]
            if len(xs) == 0:
                xs = np.where(fg.any(axis=0))[0]
            anchor = int(round((xs.min() + xs.max()) / 2)) if len(xs) > 0 else nw // 2
            scaled_crops.append((scaled_im, anchor, f["pose"]))
            
        # Pack into atlas
        ax = ay = a_rowh = 0
        placements = []
        for s_im, anchor, pose in scaled_crops:
            if ax + s_im.width + 2 > ATLAS_W:
                ax = 0
                ay += a_rowh + 2
                a_rowh = 0
            placements.append((ax, ay, s_im.width, s_im.height, anchor, pose))
            ax += s_im.width + 2
            a_rowh = max(a_rowh, s_im.height)
            
        atlas_h = ay + a_rowh + 4
        atlas_img = Image.new("RGBA", (ATLAS_W, atlas_h), (0, 0, 0, 0))
        
        sprite_txt_lines = [
            f'// PowerSlave Exhumed - Player {slot} ({char["name"]}) Sprite Atlas',
            f'// Canonical in-game height: ~98px, transparent RGBA',
            f'texture "sprites/player/{char["prefix"]}.png"',
            ""
        ]
        
        for idx, (px, py, pw, ph, anchor, pose) in enumerate(placements):
            atlas_img.paste(scaled_crops[idx][0], (px, py))
            sprite_txt_lines.append(f'// Sprite {idx}: {pose}')
            sprite_txt_lines.append(f'sprite {idx} {px} {py} {pw} {ph}')
            sprite_txt_lines.append("")
            
        # Save game-ready atlas and sprite.txt
        atlas_path = os.path.join(folder, f"{char['prefix']}.png")
        atlas_img.save(atlas_path)
        with open(os.path.join(folder, f"{char['prefix']}_sprite.txt"), "w") as f_out:
            f_out.write("\n".join(sprite_txt_lines))
            
        # Generate anim.txt
        anim_txt = [
            f'// PowerSlave Exhumed - Player {slot} ({char["name"]}) Animation Script',
            f'// 8 rotations with horizontal mirroring, matched to KEX Engine conventions',
            ""
        ]
        
        # We define basic animations: idle, walk, shoot, death
        num_sp = len(placements)
        idle_idx = 0
        walk_indices = list(range(min(4, num_sp)))
        shoot_idx = min(len(rows[0]) if len(rows) > 1 else 0, num_sp - 1)
        death_indices = list(range(max(0, num_sp - 4), num_sp))
        
        anim_txt.append(f'{char["prefix"]}_idle\n{{\n    frame\n    {{\n        delay 30')
        for rot in ["N", "NE", "E", "ES", "S", "SW", "W", "WN"]:
            flip = 1 if rot in ("ES", "E", "NE") else 0
            xoff = -(placements[idle_idx][2] - 1 - placements[idle_idx][4]) if flip else -placements[idle_idx][4]
            yoff = -placements[idle_idx][3]
            anim_txt.append(f'        rotation_{rot}\n        {{\n            sprites\n            {{\n                {{ "player/{char["prefix"]}", {idle_idx}, {xoff}, {yoff}, {flip} }}\n            }}\n        }}')
        anim_txt.append("    }\n}\n")
        
        anim_txt.append(f'{char["prefix"]}_walk\n{{\n')
        for wi in walk_indices:
            anim_txt.append(f'    frame\n    {{\n        delay 7')
            for rot in ["N", "NE", "E", "ES", "S", "SW", "W", "WN"]:
                flip = 1 if rot in ("ES", "E", "NE") else 0
                xoff = -(placements[wi][2] - 1 - placements[wi][4]) if flip else -placements[wi][4]
                yoff = -placements[wi][3]
                anim_txt.append(f'        rotation_{rot}\n        {{\n            sprites\n            {{\n                {{ "player/{char["prefix"]}", {wi}, {xoff}, {yoff}, {flip} }}\n            }}\n        }}')
            anim_txt.append("    }")
        anim_txt.append("}\n")
        
        anim_txt.append(f'{char["prefix"]}_shoot\n{{\n    frame\n    {{\n        delay 6')
        for rot in ["N", "NE", "E", "ES", "S", "SW", "W", "WN"]:
            flip = 1 if rot in ("ES", "E", "NE") else 0
            xoff = -(placements[shoot_idx][2] - 1 - placements[shoot_idx][4]) if flip else -placements[shoot_idx][4]
            yoff = -placements[shoot_idx][3]
            anim_txt.append(f'        rotation_{rot}\n        {{\n            sprites\n            {{\n                {{ "player/{char["prefix"]}", {shoot_idx}, {xoff}, {yoff}, {flip} }}\n            }}\n        }}')
        anim_txt.append("    }\n}\n")
        
        anim_txt.append(f'{char["prefix"]}_death\n{{\n')
        for di_i, di in enumerate(death_indices):
            delay = 0xffff if di_i == len(death_indices) - 1 else 7
            anim_txt.append(f'    frame\n    {{\n        delay {delay}')
            for rot in ["N", "NE", "E", "ES", "S", "SW", "W", "WN"]:
                xoff = -placements[di][4]
                yoff = -placements[di][3]
                anim_txt.append(f'        rotation_{rot}\n        {{\n            sprites\n            {{\n                {{ "player/{char["prefix"]}", {di}, {xoff}, {yoff}, 0 }}\n            }}\n        }}')
            anim_txt.append("    }")
        anim_txt.append("}\n")
        
        with open(os.path.join(folder, f"{char['prefix']}_anim.txt"), "w") as f_out:
            f_out.write("\n".join(anim_txt))
            
        # Write character card
        create_character_card(char, extracted_frames, folder)

    # Step 2: Build Master Showcase Poster
    build_roster_showcase(roster_showcase_data)
    print("Master Roster Showcase Poster built successfully.")

def create_character_card(char, extracted_frames, folder):
    """Generate a high-resolution presentation card for the player."""
    W, H = 800, 480
    card = Image.new("RGBA", (W, H), (22, 24, 28, 255))
    draw = ImageDraw.Draw(card)
    
    # Border & accents
    accent_gold = (212, 160, 23, 255)
    border_dark = (48, 52, 60, 255)
    draw.rectangle([0, 0, W-1, H-1], outline=accent_gold, width=3)
    draw.rectangle([6, 6, W-7, H-7], outline=border_dark, width=1)
    
    # Header banner
    draw.rectangle([10, 10, W-10, 60], fill=(32, 36, 44, 255))
    draw.rectangle([10, 58, W-10, 60], fill=accent_gold)
    
    title = f"PLAYER {char['slot']}: {char['name'].upper()} ({char['codename'].upper()})"
    draw.text((25, 22), title, fill=(255, 235, 175, 255))
    draw.text((25, 75), f"ROLE: {char['role']}", fill=(220, 220, 220, 255))
    draw.text((25, 100), f"THEME: {char['palette_theme']}", fill=(180, 190, 205, 255))
    draw.text((25, 125), f"GEAR: {char['gear']}", fill=(190, 190, 190, 255))
    
    # Bio text wrap
    bio_lines = []
    words = char['bio'].split()
    cur = ""
    for w in words:
        if len(cur + " " + w) < 45:
            cur += " " + w
        else:
            bio_lines.append(cur.strip())
            cur = w
    if cur:
        bio_lines.append(cur.strip())
        
    y_bio = 160
    draw.text((25, y_bio), "EXPEDITION DOSSIER:", fill=accent_gold)
    y_bio += 22
    for bl in bio_lines:
        draw.text((25, y_bio), bl, fill=(170, 175, 185, 255))
        y_bio += 20
        
    # Draw preview sprite boxes on the right
    sample_poses = [
        ("IDLE FRONT", extracted_frames[0]["crop"]),
        ("SIDE PROFILE", extracted_frames[min(2, len(extracted_frames)-1)]["crop"]),
        ("COMBAT FIRE", extracted_frames[min(len(extracted_frames)//2, len(extracted_frames)-1)]["crop"]),
        ("FALLEN", extracted_frames[-1]["crop"])
    ]
    
    start_x = 420
    box_w = 85
    box_h = 220
    
    for i, (label, cr) in enumerate(sample_poses):
        bx = start_x + i * 92
        by = 220
        draw.rectangle([bx, by, bx + box_w, by + box_h], fill=(15, 17, 20, 255), outline=(60, 65, 75, 255))
        draw.text((bx + 4, by + 4), label, fill=(160, 160, 160, 255))
        
        # Scale sprite to fit box
        scale = min((box_w - 10) / cr.width, (box_h - 35) / cr.height)
        sw = max(10, int(cr.width * scale))
        sh = max(10, int(cr.height * scale))
        sc_cr = cr.resize((sw, sh), Image.LANCZOS)
        
        px = bx + (box_w - sw) // 2
        py = by + box_h - sh - 8
        card.paste(sc_cr, (px, py), sc_cr)
        
    card.save(os.path.join(folder, "character_card.png"))

def build_roster_showcase(roster_data):
    """Build the grand 00_ROSTER_SHOWCASE.png showcasing all 8 players side by side."""
    W = 2400
    H = 1400
    img = Image.new("RGBA", (W, H), (14, 16, 20, 255))
    draw = ImageDraw.Draw(img)
    
    accent_gold = (212, 160, 23, 255)
    border_muted = (50, 56, 68, 255)
    
    # Outer frame
    draw.rectangle([10, 10, W-10, H-10], outline=accent_gold, width=4)
    draw.rectangle([16, 16, W-16, H-16], outline=border_muted, width=2)
    
    # Title Header
    draw.rectangle([25, 25, W-25, 130], fill=(24, 28, 36, 255), outline=accent_gold, width=2)
    draw.text((50, 38), "POWERSLAVE EXHUMED: CO-OP EXPEDITION ROSTER", fill=(255, 230, 150, 255))
    draw.text((50, 75), "8 DISTINCT PLAYABLE HUMAN OPERATIVES - AUTHENTIC 1996 RETRO FPS PIXEL ARTWORK", fill=(200, 210, 225, 255))
    draw.text((50, 102), "Custom high-contrast silhouettes, unique attire, distinctive 90s gear & weapons. Zero simple recolors.", fill=(160, 170, 185, 255))
    
    # Grid: 8 columns (one per player)
    col_w = (W - 80) // 8
    
    for i, data in enumerate(roster_data):
        char = data["char"]
        cx = 40 + i * col_w
        cy = 150
        cw = col_w - 12
        ch = H - 180
        
        # Column background
        bg_col = (20, 24, 30, 255) if i % 2 == 0 else (26, 30, 38, 255)
        draw.rectangle([cx, cy, cx + cw, cy + ch], fill=bg_col, outline=border_muted, width=1)
        
        # Slot banner
        slot_banner_h = 75
        draw.rectangle([cx, cy, cx + cw, cy + slot_banner_h], fill=(36, 42, 54, 255))
        draw.rectangle([cx, cy + slot_banner_h - 3, cx + cw, cy + slot_banner_h], fill=accent_gold)
        
        draw.text((cx + 8, cy + 8), f"PLAYER {char['slot']}", fill=accent_gold)
        draw.text((cx + 8, cy + 28), char['codename'].upper(), fill=(255, 255, 255, 255))
        
        # Name
        first_name = char['name'].split()[0] + (" " + char['name'].split()[1] if len(char['name'].split()) > 1 else "")
        draw.text((cx + 8, cy + 50), first_name[:18], fill=(200, 205, 215, 255))
        
        # Primary Sprite: Front Standing Idle
        front_cr = data["idle_front"]
        max_h = 360
        scale = min((cw - 24) / front_cr.width, max_h / front_cr.height)
        sw = max(20, int(front_cr.width * scale))
        sh = max(20, int(front_cr.height * scale))
        scaled_front = front_cr.resize((sw, sh), Image.LANCZOS)
        
        px = cx + (cw - sw) // 2
        py = cy + 100 + (max_h - sh)
        img.paste(scaled_front, (px, py), scaled_front)
        
        # Divider
        draw.line([cx + 10, cy + 480, cx + cw - 10, cy + 480], fill=(60, 68, 80, 255), width=1)
        
        # Secondary Sprite: Action / Shooting
        action_cr = data["action"]
        max_h_act = 260
        scale_act = min((cw - 24) / action_cr.width, max_h_act / action_cr.height)
        sw_act = max(20, int(action_cr.width * scale_act))
        sh_act = max(20, int(action_cr.height * scale_act))
        scaled_act = action_cr.resize((sw_act, sh_act), Image.LANCZOS)
        
        px_act = cx + (cw - sw_act) // 2
        py_act = cy + 490 + (max_h_act - sh_act)
        img.paste(scaled_act, (px_act, py_act), scaled_act)
        draw.text((cx + 8, cy + 485), "COMBAT / SHOOT", fill=(140, 150, 165, 255))
        
        # Divider
        draw.line([cx + 10, cy + 770, cx + cw - 10, cy + 770], fill=(60, 68, 80, 255), width=1)
        
        # Tertiary Sprite: Death / Fallen
        death_cr = data["death"]
        max_h_d = 160
        scale_d = min((cw - 24) / death_cr.width, max_h_d / death_cr.height)
        sw_d = max(20, int(death_cr.width * scale_d))
        sh_d = max(20, int(death_cr.height * scale_d))
        scaled_d = death_cr.resize((sw_d, sh_d), Image.LANCZOS)
        
        px_d = cx + (cw - sw_d) // 2
        py_d = cy + 780 + (max_h_d - sh_d)
        img.paste(scaled_d, (px_d, py_d), scaled_d)
        draw.text((cx + 8, cy + 775), "DEATH / FALLEN", fill=(140, 150, 165, 255))
        
        # Character Role & Palette at bottom
        draw.rectangle([cx + 6, cy + 960, cx + cw - 6, cy + ch - 10], fill=(22, 26, 34, 255), outline=(45, 52, 64, 255))
        draw.text((cx + 12, cy + 968), "ROLE:", fill=accent_gold)
        
        role_words = char['role'].split()
        r1 = " ".join(role_words[:2])
        r2 = " ".join(role_words[2:])
        draw.text((cx + 12, cy + 988), r1[:20], fill=(220, 220, 220, 255))
        if r2:
            draw.text((cx + 12, cy + 1006), r2[:20], fill=(200, 200, 200, 255))
            
        draw.text((cx + 12, cy + 1032), "THEME:", fill=(160, 190, 220, 255))
        draw.text((cx + 12, cy + 1052), char['palette_theme'][:22], fill=(180, 180, 180, 255))
        
    out_poster_path = os.path.join(OUT_ROOT, "00_ROSTER_SHOWCASE.png")
    img.save(out_poster_path)
    print("00_ROSTER_SHOWCASE.png saved at:", out_poster_path)

if __name__ == "__main__":
    process_all()
