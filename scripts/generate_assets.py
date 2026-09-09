import os
import math
import struct
import wave

# Tạo thư mục nếu chưa có
os.makedirs("assets/cards", exist_ok=True)
os.makedirs("assets/frames", exist_ok=True)
os.makedirs("assets/characters", exist_ok=True)
os.makedirs("assets/audio", exist_ok=True)
os.makedirs("assets/fonts", exist_ok=True)

try:
    from PIL import Image, ImageDraw, ImageFilter, ImageFont
    HAS_PIL = True
except ImportError:
    HAS_PIL = False

def create_gradient_box(width, height, top_color, bot_color):
    img = Image.new("RGBA", (width, height), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)
    for y in range(height):
        ratio = y / max(1, height - 1)
        r = int(top_color[0] * (1 - ratio) + bot_color[0] * ratio)
        g = int(top_color[1] * (1 - ratio) + bot_color[1] * ratio)
        b = int(top_color[2] * (1 - ratio) + bot_color[2] * ratio)
        a = int(top_color[3] * (1 - ratio) + bot_color[3] * ratio) if len(top_color) > 3 else 255
        draw.line([(0, y), (width, y)], fill=(r, g, b, a))
    return img

def generate_card_art(filename, title, theme="paladin"):
    if not HAS_PIL:
        return
    # Khung tranh minh họa theo GDD 3.1: 420 x 320 px
    w, h = 420, 320
    if theme == "paladin":
        img = create_gradient_box(w, h, (40, 25, 10), (10, 8, 15))
        draw = ImageDraw.Draw(img)
        # Tia sáng thánh quang (Holy light beams)
        for i in range(7):
            draw.polygon([(w//2, 0), (i * 70, h), (i * 70 + 40, h)], fill=(255, 230, 120, 35))
        # Biểu tượng đại búa hoặc kiếm thánh
        draw.rectangle([(w//2 - 8, 60), (w//2 + 8, 260)], fill=(255, 220, 100, 240))
        draw.rectangle([(w//2 - 45, 100), (w//2 + 45, 116)], fill=(255, 240, 150, 240))
    elif theme == "saintess":
        img = create_gradient_box(w, h, (25, 35, 55), (12, 15, 25))
        draw = ImageDraw.Draw(img)
        # Hào quang thiên sứ (Angelic halo)
        draw.ellipse([(w//2 - 65, 70), (w//2 + 65, 200)], outline=(220, 235, 255, 200), width=4)
        draw.ellipse([(w//2 - 50, 85), (w//2 + 50, 185)], outline=(255, 245, 180, 180), width=2)
        # Hạt bụi sáng thánh linh
        for step in range(12):
            ang = step * (math.pi / 6)
            cx = int(w//2 + 85 * math.cos(ang))
            cy = int(135 + 85 * math.sin(ang))
            draw.ellipse([(cx - 4, cy - 4), (cx + 4, cy + 4)], fill=(255, 255, 220, 220))
    elif theme == "knight":
        img = create_gradient_box(w, h, (20, 35, 50), (8, 14, 22))
        draw = ImageDraw.Draw(img)
        # Khiên tháp dũng mãnh (Colossal Tower Shield)
        points = [(w//2 - 50, 60), (w//2 + 50, 60), (w//2 + 45, 220), (w//2, 270), (w//2 - 45, 220)]
        draw.polygon(points, fill=(50, 80, 120, 220), outline=(100, 180, 255, 240))
        draw.line([(w//2, 60), (w//2, 265)], fill=(180, 220, 255, 220), width=3)
    else: # void / enemy
        img = create_gradient_box(w, h, (35, 12, 45), (10, 5, 15))
        draw = ImageDraw.Draw(img)
        draw.ellipse([(w//2 - 70, 70), (w//2 + 70, 210)], fill=(80, 20, 100, 180), outline=(200, 50, 220, 230), width=3)

    img.save(f"assets/cards/{filename}.png")
    print(f"Generated card art: assets/cards/{filename}.png")

def generate_frames():
    if not HAS_PIL:
        return
    # Kích thước khung chuẩn GDD 3.1: 500 x 700 px
    w, h = 500, 700
    themes = {
        "frame_paladin": (240, 190, 40),   # Vàng kim
        "frame_saintess": (220, 235, 255), # Trắng bạc
        "frame_knight": (70, 150, 240)     # Xanh dương thép
    }
    for name, col in themes.items():
        img = Image.new("RGBA", (w, h), (0, 0, 0, 0))
        draw = ImageDraw.Draw(img)
        # Viền ngoài dày 8px
        draw.rectangle([(8, 8), (w - 9, h - 9)], outline=col, width=6)
        draw.rectangle([(16, 16), (w - 17, h - 17)], outline=(col[0]//2, col[1]//2, col[2]//2, 180), width=2)
        # Họa tiết góc nhà thờ (Filigree corners)
        corner_len = 35
        for x, y in [(16, 16), (w - 17, 16), (16, h - 17), (w - 17, h - 17)]:
            dx = corner_len if x < w//2 else -corner_len
            dy = corner_len if y < h//2 else -corner_len
            draw.line([(x, y), (x + dx, y)], fill=col, width=3)
            draw.line([(x, y), (x, y + dy)], fill=col, width=3)
            draw.line([(x + dx//2, y + dy//2), (x, y + dy)], fill=col, width=2)

        img.save(f"assets/frames/{name}.png")
        print(f"Generated frame: assets/frames/{name}.png")

def generate_characters():
    if not HAS_PIL:
        return
    # Boss: Void Apostle
    w, h = 260, 340
    img = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)
    # Áo choàng quỷ tím đen (Twisted dark robes)
    draw.polygon([(w//2, 40), (w - 30, h - 20), (30, h - 20)], fill=(30, 15, 45, 240), outline=(150, 40, 200, 220))
    # Mắt quỷ đỏ rực (Glowing red eyes)
    draw.ellipse([(w//2 - 25, 110), (w//2 - 10, 122)], fill=(255, 30, 40, 255))
    draw.ellipse([(w//2 + 10, 110), (w//2 + 25, 122)], fill=(255, 30, 40, 255))
    # Cổ tự ma pháp tím (Glowing purple runes)
    draw.ellipse([(w//2 - 40, 160), (w//2 + 40, 240)], outline=(190, 60, 255, 200), width=3)
    img.save("assets/characters/void_apostle.png")

    # Minions
    for minion_name in ["shadow_minion", "cultist_fiend"]:
        m_img = Image.new("RGBA", (180, 240), (0, 0, 0, 0))
        m_draw = ImageDraw.Draw(m_img)
        m_draw.polygon([(90, 30), (160, 220), (20, 220)], fill=(45, 20, 60, 230), outline=(120, 40, 160, 200))
        m_draw.ellipse([(70, 80), (82, 90)], fill=(255, 60, 60, 255))
        m_draw.ellipse([(98, 80), (110, 90)], fill=(255, 60, 60, 255))
        m_img.save(f"assets/characters/{minion_name}.png")

def generate_wav(filename, freq_start, freq_end, duration_sec, sample_rate=44100):
    num_samples = int(duration_sec * sample_rate)
    with wave.open(f"assets/audio/{filename}.wav", "w") as wav_file:
        wav_file.setnchannels(1)
        wav_file.setsampwidth(2)
        wav_file.setframerate(sample_rate)
        for i in range(num_samples):
            t = i / sample_rate
            progress = i / num_samples
            freq = freq_start * (1 - progress) + freq_end * progress
            # Giảm dần âm lượng theo hàm mũ
            envelope = math.exp(-3.5 * progress)
            val = math.sin(2.0 * math.pi * freq * t) * envelope
            sample = int(val * 32767 * 0.7)
            wav_file.writeframesraw(struct.pack('<h', max(-32768, min(32767, sample))))
    print(f"Generated sound: assets/audio/{filename}.wav")

if __name__ == "__main__":
    generate_card_art("vanguard_wall", "Thep Ve Binh", "knight")
    generate_card_art("dawn_prayer", "Loi Nguyen Binh Minh", "saintess")
    generate_card_art("sacred_cleanse", "Thanh Tay Linh Hon", "saintess")
    generate_card_art("holy_smite", "Trung Phat Thanh Quang", "paladin")
    generate_card_art("heavenly_judgment", "Phan Quyet Troi Cao", "paladin")

    generate_frames()
    generate_characters()

    # Tạo âm thanh theo GDD mục 4.1: chuong_thanh.wav (tiếng chuông thánh), kiem_chem.wav (tiếng chém kim loại)
    generate_wav("chuong_thanh", 587.33, 440.0, 1.8) # D5 chime tone
    generate_wav("kiem_chem", 1200.0, 250.0, 0.35)    # Metal sword slash
