import customtkinter as ctk
import tkinter as tk
from tkinter import messagebox
import subprocess
import threading
import serial
import serial.tools.list_ports
import os
import re

# Configuração visual do tema
ctk.set_appearance_mode("Dark")
ctk.set_default_color_theme("blue")

class AnimatronicStudio(ctk.CTk):
    def __init__(self):
        super().__init__()
        self.title("Animatronic Eye Studio")
        self.geometry("950x650")
        
        self.grid_columnconfigure(0, weight=1)
        self.grid_columnconfigure(1, weight=1)
        self.grid_rowconfigure(0, weight=1)
        
        # ====================================================
        # LADO ESQUERDO: CONFIGURAÇÃO DOS SERVOS
        # ====================================================
        self.left_frame = ctk.CTkFrame(self)
        self.left_frame.grid(row=0, column=0, padx=10, pady=10, sticky="nsew")
        
        self.label_title = ctk.CTkLabel(self.left_frame, text="⚙️ Configuração dos Servos", font=ctk.CTkFont(size=20, weight="bold"))
        self.label_title.pack(pady=10)
        
        self.scrollable_frame = ctk.CTkScrollableFrame(self.left_frame)
        self.scrollable_frame.pack(fill="both", expand=True, padx=10, pady=10)
        
        self.servos = []
        
        # Carregar servos padrão
        self.add_servo_ui("Olho Pan (X)", 13, 45, 135, 90, False)
        self.add_servo_ui("Olho Tilt (Y)", 12, 60, 120, 90, True)
        self.add_servo_ui("Pálpebra Sup Esq", 14, 20, 160, 90, False)
        self.add_servo_ui("Pálpebra Inf Esq", 27, 20, 160, 90, True)
        self.add_servo_ui("Pálpebra Sup Dir", 26, 20, 160, 90, False)
        self.add_servo_ui("Pálpebra Inf Dir", 25, 20, 160, 90, True)
        
        self.btn_add = ctk.CTkButton(self.left_frame, text="+ Adicionar Novo Servo", command=lambda: self.add_servo_ui("Novo Servo", 0, 0, 180, 90, 0))
        self.btn_add.pack(pady=10)
        
        # ====================================================
        # LADO DIREITO: CONTROLES E MONITOR
        # ====================================================
        self.right_frame = ctk.CTkFrame(self)
        self.right_frame.grid(row=0, column=1, padx=10, pady=10, sticky="nsew")
        
        self.btn_upload = ctk.CTkButton(self.right_frame, text="⚡ GERAR CÓDIGO E UPLOAD ⚡", font=ctk.CTkFont(size=18, weight="bold"), height=50, fg_color="#2ecc71", hover_color="#27ae60", command=self.upload_thread)
        self.btn_upload.pack(pady=20, padx=20, fill="x")
        
        self.log_box = ctk.CTkTextbox(self.right_frame, state="disabled", font=("Consolas", 12))
        self.log_box.pack(fill="both", expand=True, padx=10, pady=10)
        
        # ====================================================
        # SERIAL MONITOR
        # ====================================================
        self.serial_frame = ctk.CTkFrame(self.right_frame)
        self.serial_frame.pack(fill="x", padx=10, pady=10)
        
        ctk.CTkLabel(self.serial_frame, text="Porta:").pack(side="left", padx=5)
        self.ports_combobox = ctk.CTkComboBox(self.serial_frame, values=self.get_ports(), width=100)
        self.ports_combobox.pack(side="left", padx=5)
        
        self.btn_refresh = ctk.CTkButton(self.serial_frame, text="🔄 Atualizar", width=80, command=lambda: self.ports_combobox.configure(values=self.get_ports()))
        self.btn_refresh.pack(side="left", padx=5)
        
        self.btn_connect = ctk.CTkButton(self.serial_frame, text="🔌 Conectar Monitor", width=140, command=self.toggle_serial)
        self.btn_connect.pack(side="right", padx=5)
        
        self.serial_conn = None
        self.serial_thread_active = False
        
        self.log("Bem-vindo ao Animatronic Eye Studio!")
        self.log("Configure os servos e clique em Upload para compilar para o ESP32.")

    def add_servo_ui(self, name, pin, vmin, vmax, vinit, inverted):
        frame = ctk.CTkFrame(self.scrollable_frame, fg_color="#2b2b2b")
        frame.pack(fill="x", pady=5)
        
        # Variaveis para ler da tela
        v_name = tk.StringVar(value=name)
        v_pin = tk.StringVar(value=str(pin))
        v_min = tk.StringVar(value=str(vmin))
        v_max = tk.StringVar(value=str(vmax))
        v_init = tk.StringVar(value=str(vinit))
        v_inv = tk.BooleanVar(value=bool(inverted))
        
        row1 = ctk.CTkFrame(frame, fg_color="transparent")
        row1.pack(fill="x", padx=5, pady=2)
        ctk.CTkEntry(row1, textvariable=v_name, width=140).pack(side="left", padx=2)
        ctk.CTkLabel(row1, text="Pino ESP32:").pack(side="left", padx=5)
        ctk.CTkEntry(row1, textvariable=v_pin, width=40).pack(side="left", padx=2)
        ctk.CTkButton(row1, text="X", width=30, fg_color="#e74c3c", hover_color="#c0392b", command=lambda f=frame, item=(v_name, v_pin, v_min, v_max, v_init, v_inv): self.remove_servo(f, item)).pack(side="right", padx=2)
        
        row2 = ctk.CTkFrame(frame, fg_color="transparent")
        row2.pack(fill="x", padx=5, pady=2)
        ctk.CTkLabel(row2, text="Min:").pack(side="left")
        ctk.CTkEntry(row2, textvariable=v_min, width=40).pack(side="left", padx=2)
        ctk.CTkLabel(row2, text="Max:").pack(side="left", padx=2)
        ctk.CTkEntry(row2, textvariable=v_max, width=40).pack(side="left", padx=2)
        ctk.CTkLabel(row2, text="Descanso:").pack(side="left", padx=2)
        ctk.CTkEntry(row2, textvariable=v_init, width=40).pack(side="left", padx=2)
        ctk.CTkCheckBox(row2, text="Inverter Dir.", variable=v_inv).pack(side="right", padx=2)
        
        self.servos.append((v_name, v_pin, v_min, v_max, v_init, v_inv))
        
    def remove_servo(self, frame, item):
        frame.destroy()
        self.servos.remove(item)

    def log(self, text):
        self.log_box.configure(state="normal")
        self.log_box.insert("end", text + "\n")
        self.log_box.see("end")
        self.log_box.configure(state="disabled")

    def upload_thread(self):
        self.btn_upload.configure(state="disabled")
        threading.Thread(target=self.do_upload, daemon=True).start()

    def do_upload(self):
        try:
            self.log("\n[1/3] Gerando novo código C++ a partir da interface...")
            
            # Monta o struct no formato C++
            config_code = f"const int NUM_SERVOS = {len(self.servos)};\nMotorConfig configServos[NUM_SERVOS] = {{\n"
            for i, s in enumerate(self.servos):
                name = s[0].get()
                pin = s[1].get()
                vmin = s[2].get()
                vmax = s[3].get()
                vinit = s[4].get()
                vinv = "true" if s[5].get() else "false"
                config_code += f'    {{"{name}", {pin}, {vmin}, {vmax}, {vinit}, {vinv}}}'
                if i < len(self.servos) - 1:
                    config_code += ",\n"
                else:
                    config_code += "\n"
            config_code += "};\n"
            
            # Lê o arquivo main.cpp para injetar as configurações
            filepath = "src/main.cpp"
            if not os.path.exists(filepath):
                self.log(f"Erro: Arquivo {filepath} não encontrado!")
                return
                
            with open(filepath, "r", encoding="utf-8") as f:
                content = f.read()
                
            # Regex para substituir o bloco de código
            pattern = r"(// --- AUTO-GENERATED CONFIG START ---).*?(// --- AUTO-GENERATED CONFIG END ---)"
            replacement = r"\1\n" + config_code + r"\2"
            new_content = re.sub(pattern, replacement, content, flags=re.DOTALL)
            
            with open(filepath, "w", encoding="utf-8") as f:
                f.write(new_content)
                
            self.log("[2/3] Código atualizado! Iniciando PlatformIO...")
            
            # Desconecta o Serial Monitor temporariamente para não falhar o upload
            if self.serial_thread_active:
                self.log("Desconectando Serial Monitor temporariamente para o Upload...")
                self.toggle_serial()
                
            # Chama o PlatformIO
            process = subprocess.Popen(["pio", "run", "-t", "upload"], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
            for line in process.stdout:
                self.log(line.strip())
            
            process.wait()
            if process.returncode == 0:
                self.log("\n[3/3] === UPLOAD CONCLUÍDO COM SUCESSO ===")
                messagebox.showinfo("Sucesso!", "Código gravado no ESP32 com sucesso!\nSeu olho animatrônico já deve estar se movendo.")
            else:
                self.log("\n=== ERRO NO UPLOAD ===")
                self.log("Verifique se o ESP32 está conectado e se a porta não está ocupada.")
                
        except Exception as e:
            self.log(f"\nErro Crítico: {str(e)}")
            
        finally:
            self.btn_upload.configure(state="normal")

    def get_ports(self):
        ports = [p.device for p in serial.tools.list_ports.comports()]
        if not ports:
            return ["Nenhuma"]
        return ports

    def toggle_serial(self):
        if self.serial_thread_active:
            self.serial_thread_active = False
            if self.serial_conn:
                self.serial_conn.close()
            self.btn_connect.configure(text="🔌 Conectar Monitor", fg_color="#3498db", hover_color="#2980b9")
            self.log("Monitor Serial Desconectado.")
        else:
            port = self.ports_combobox.get()
            if not port or port == "Nenhuma":
                self.log("Selecione uma porta válida!")
                return
            try:
                self.serial_conn = serial.Serial(port, 115200, timeout=1)
                self.serial_thread_active = True
                self.btn_connect.configure(text="🛑 Desconectar", fg_color="#e74c3c", hover_color="#c0392b")
                self.log(f"--- CONECTADO A {port} (115200 baud) ---")
                threading.Thread(target=self.serial_read_loop, daemon=True).start()
            except Exception as e:
                self.log(f"Erro ao conectar na porta {port}: {str(e)}")

    def serial_read_loop(self):
        while self.serial_thread_active and self.serial_conn.is_open:
            try:
                line = self.serial_conn.readline().decode('utf-8', errors='ignore').strip()
                if line:
                    self.log(f"> {line}")
            except:
                pass

if __name__ == "__main__":
    app = AnimatronicStudio()
    app.mainloop()
