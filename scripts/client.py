import socket
import struct
import time
import random

# Server configuration
HOST = '127.0.0.1'
PORT = 8080

# Top 20 Tech Companies
TICKERS = [
    "AAPL", "MSFT", "GOOG", "AMZN", "NVDA", "META", "TSLA", "NFLX", 
    "INTC", "AMD",  "CSCO", "ADBE", "CRM",  "ORCL", "IBM",  "QCOM", 
    "TXN",  "AVGO", "NOW",  "UBER"
]

# Approximate base prices for realism
BASE_PRICES = {
    0: 150.00,  1: 300.00,  2: 120.00,  3: 130.00,  4: 135.50,  5: 250.00,  6: 200.00,  7: 400.00,
    8: 30.00,   9: 110.00,  10: 50.00,  11: 450.00, 12: 210.00, 13: 115.00, 14: 140.00, 15: 125.00,
    16: 160.00, 17: 850.00, 18: 500.00, 19: 45.00
}

# TCP Socket setup
print(f"\033[96m[SYSTEM] Connecting to Exchange Server at {HOST}:{PORT}...\033[0m")
try:
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    s.connect((HOST, PORT))
    print("\033[92m[SYSTEM] Connected Successfully!\033[0m\n")
except Exception as e:
    print(f"\033[91mFailed to connect: {e}\033[0m")
    exit(1)

# Protocol Format: <bbHIQ (Little-Endian: 1-byte, 1-byte, 2-byte, 4-byte, 8-byte = 16 bytes)
def send_order(side, instr_id, price_decimal, qty):
    # Convert decimal to Fixed-Point Integer (Multiplier: 100)
    price_integer = int(price_decimal * 100)
    
    # Pack the binary struct
    payload = struct.pack('<bbHIQ', 1, side, instr_id, qty, price_integer)
    s.sendall(payload)
    
    # ANSI Colors for presentation
    GREEN = '\033[92m'
    RED = '\033[91m'
    RESET = '\033[0m'
    ticker = TICKERS[instr_id]
    
    if side == 0:
        print(f"[{GREEN}SENT{RESET}] {GREEN}BUY  {qty:<4} {ticker:<5} @ ${price_decimal:.2f}{RESET}  (ID: {instr_id}, Int: {price_integer})")
    else:
        print(f"[{RED}SENT{RESET}] {RED}SELL {qty:<4} {ticker:<5} @ ${price_decimal:.2f}{RESET}  (ID: {instr_id}, Int: {price_integer})")

print("\033[96m--- Commencing Top 20 Tech Market Maker Burst ---\033[0m")

for i in range(40):
    side = random.choice([0, 1])
    instr_id = random.randint(0, 19)
    base_price = BASE_PRICES[instr_id]
    
    # Generate realistic tight market oscillations (+/- $0.05)
    offset = random.uniform(-0.05, 0.05)
    price_decimal = round(base_price + offset, 2)
    
    qty = random.choice([100, 200, 500, 1000])
    send_order(side, instr_id, price_decimal, qty)
    
    time.sleep(0.05) 

print("\n\033[96m--- Burst Complete ---\033[0m")
print("Check the C++ Server terminal to see the trades!")
s.close()
