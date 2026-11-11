import os

def main():
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    hlsl_path = os.path.join(root, 'third_party', 'smaa', 'SMAA.hlsl')
    inc_path = os.path.join(root, 'third_party', 'smaa', 'smaa_hlsl.inc')
    
    with open(hlsl_path, 'rb') as f:
        data = f.read()
    
    # Replace non-ascii bytes >= 128 with '?'
    clean_bytes = bytes([b if b < 128 else ord('?') for b in data])
    text = clean_bytes.decode('latin1')
    lines = text.replace('\r\n', '\n').replace('\r', '\n').split('\n')
    
    out_lines = []
    for line in lines:
        escaped = line.replace('\\', '\\\\').replace('"', '\\"')
        out_lines.append(f'"{escaped}\\n"')
        
    with open(inc_path, 'w', newline='\n', encoding='utf-8') as f:
        f.write('\n'.join(out_lines))
        
    print(f"Generated {inc_path} ({len(out_lines)} lines)")

if __name__ == '__main__':
    main()
