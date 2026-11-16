"""Local MP3 -> FFmpeg s16le mono PCM -> CH32 USB CDC. Never flashes song data."""
from __future__ import annotations
import argparse
import binascii
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import time

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE / "packages"))
MAGIC, RESPONSE = b"APCM", b"ARSP"
HEADER = struct.Struct("<4sBBHII")
RATE = 16000
ERRORS = {
    1: "帧参数或音频格式错误", 2: "扬声器初始化失败", 3: "帧序号错误/没有开始会话",
    4: "PCM写入失败", 5: "CRC错误", 6: "输入超时", 7: "USB连接重置",
    8: "音频欠载：电脑供数或USB/线程服务不及时", 9: "DMA错误或中断延迟过长",
    10: "播放被板上其他功能取消",
}

def make_frame(kind: int, seq: int, payload: bytes = b"") -> bytes:
    prefix = struct.pack("<4sBBHI", MAGIC, 1, kind, len(payload), seq)
    crc = binascii.crc32(prefix + payload) & 0xFFFFFFFF
    return prefix + struct.pack("<I", crc) + payload

class Player:
    def __init__(self, port):
        self.port = port
        self.rx = bytearray()
        self.seq = 0

    def read_response(self, timeout=5.0):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            pos = self.rx.find(RESPONSE)
            if pos < 0:
                if len(self.rx) > 3:
                    del self.rx[:-3]
            elif pos > 0:
                del self.rx[:pos]
            if self.rx.startswith(RESPONSE) and len(self.rx) >= 16:
                raw = bytes(self.rx[:16]); del self.rx[:16]
                magic, version, kind, code, seq, crc = HEADER.unpack(raw)
                if version != 1 or binascii.crc32(raw[:12]) & 0xFFFFFFFF != crc:
                    raise RuntimeError("开发板回复CRC或版本错误")
                if kind == 2:
                    raise RuntimeError(f"开发板错误 {code}: {ERRORS.get(code, '未知错误')}; seq={seq}")
                return kind, code, seq
            self.rx.extend(self.port.read(max(1, min(self.port.in_waiting, 4096))))
        raise TimeoutError("等待开发板回复超时：确认已烧录新固件、usb_audio_on，并选择新增USB CDC COM口")

    def send(self, kind: int, payload: bytes = b""):
        seq = self.seq
        frame = make_frame(kind, seq, payload)
        offset = 0
        while offset < len(frame):
            count = self.port.write(frame[offset:])
            if not count:
                raise TimeoutError("USB写入超时")
            offset += count
        response, code, reply_seq = self.read_response()
        if response != 0 or reply_seq != seq:
            raise RuntimeError(f"回复顺序不匹配：expected ACK {seq}, got type={response} seq={reply_seq}")
        self.seq += 1
        return seq

    def stop(self):
        """Best effort after Ctrl+C/error; tolerate old ACK/error responses."""
        seq = 0xFFFFFFFE
        try:
            self.port.write(make_frame(4, seq))
            deadline = time.monotonic() + 1.0
            while time.monotonic() < deadline:
                try:
                    kind, _, reply_seq = self.read_response(0.25)
                    if kind == 0 and reply_seq == seq:
                        return
                except (RuntimeError, TimeoutError):
                    pass
        except Exception:
            pass

def main(argv=None):
    parser = argparse.ArgumentParser(description="通过USB CDC让CH32播放电脑MP3；Ctrl+C停止")
    parser.add_argument("--port", help="新增USB CDC端口，例如COM7；不要选原RTT控制台")
    parser.add_argument("--file", default=r"D:\MicrosoftDownload\郑智化+-+水手.mp3")
    parser.add_argument("--ffmpeg", default=str(HERE / "ffmpeg.exe"))
    parser.add_argument("--volume", type=int, default=70, help="开发板相对DAC音量0..100，默认70")
    parser.add_argument("--seconds", type=float, help="仅播放前N秒，便于首次验收")
    parser.add_argument("--list", action="store_true", help="只列出COM口")
    parser.add_argument("--check", action="store_true", help="只检查文件、解码器与依赖，不打开串口")
    args = parser.parse_args(argv)
    import serial
    from serial.tools import list_ports
    if args.list:
        for port in list_ports.comports():
            print(f"{port.device}: {port.description}; {port.hwid}")
        return 0
    if not 0 <= args.volume <= 100 or (args.seconds is not None and args.seconds <= 0):
        parser.error("volume应为0..100，seconds应大于0")
    song, ffmpeg = Path(args.file), Path(args.ffmpeg)
    if not song.is_file() or not ffmpeg.is_file():
        raise FileNotFoundError(f"检查MP3及FFmpeg路径：{song}; {ffmpeg}")
    if args.check:
        subprocess.run([str(ffmpeg), "-hide_banner", "-loglevel", "error", "-i", str(song), "-t", "0.1", "-f", "null", "-"], check=True)
        print("MP3、FFmpeg和pyserial检查通过；没有打开串口，也没有发送数据。")
        return 0
    if not args.port:
        parser.error("请指定 --port COM号，或使用 --list 查看")

    command = [str(ffmpeg), "-nostdin", "-hide_banner", "-loglevel", "error", "-i", str(song), "-vn"]
    if args.seconds is not None:
        command += ["-t", str(args.seconds)]
    command += ["-ac", "1", "-ar", str(RATE), "-acodec", "pcm_s16le", "-f", "s16le", "pipe:1"]
    process = None
    player = None
    port = None
    completed = False
    with tempfile.TemporaryFile() as decoder_log:
        try:
            port = serial.Serial(args.port, 115200, timeout=0.05, write_timeout=5, rtscts=False, dsrdtr=False)
            port.dtr = True
            port.reset_input_buffer()
            player = Player(port)
            process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=decoder_log, creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
            player.send(1, struct.pack("<IBBH", RATE, args.volume, 1, 16))
            print(f"播放：{song.name} → {args.port}；16kHz/16bit/单声道；音量={args.volume}；Ctrl+C停止")
            total = 0
            last_print = 0
            while True:
                block = process.stdout.read(512)
                if not block:
                    break
                if len(block) & 1:
                    raise RuntimeError("解码器返回了不完整的16位样本")
                player.send(2, block)
                total += len(block)
                if total - last_print >= RATE * 2:
                    print(f"\r已发送音频 {total / (RATE * 2):.1f} 秒", end="", flush=True)
                    last_print = total
            if process.wait(timeout=5) != 0:
                decoder_log.seek(0)
                raise RuntimeError("FFmpeg解码失败：" + decoder_log.read()[-2000:].decode("utf-8", errors="replace"))
            seq = player.send(3)
            kind, _, reply_seq = player.read_response(5)
            if kind != 1 or reply_seq != seq:
                raise RuntimeError("没有收到正确的播放完成回复")
            completed = True
            print(f"\n开发板已确认播完；共 {total} 字节PCM，约 {total / (RATE * 2):.2f} 秒。")
        except KeyboardInterrupt:
            print("\n已请求停止。")
            return 130
        finally:
            if player is not None and not completed and port is not None and port.is_open:
                player.stop()
            if port is not None:
                port.close()
            if process is not None:
                if process.poll() is None:
                    process.terminate()
                    process.wait(timeout=5)
                if process.stdout:
                    process.stdout.close()
    return 0 if completed else 1

if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as error:
        print(f"\n错误：{error}", file=sys.stderr)
        sys.exit(1)
