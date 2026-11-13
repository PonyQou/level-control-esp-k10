# -*- coding: utf-8 -*-
"""实验报告 MD → PDF：markdown 转 HTML 后用 Chrome 无头模式打印。
用法：python build_pdf.py
"""
import subprocess, sys, os
sys.stdout.reconfigure(encoding='utf-8', errors='replace')
import markdown

HERE = os.path.dirname(os.path.abspath(__file__))
MD = os.path.join(HERE, "实验报告.md")
HTML = os.path.join(HERE, "实验报告.html")
PDF = os.path.join(HERE, "实验报告.pdf")
CHROME = r"C:\Program Files\Google\Chrome\Application\chrome.exe"

CSS = """
body { font-family: "Microsoft YaHei", sans-serif; max-width: 900px;
       margin: 32px auto; padding: 0 16px; color: #222; line-height: 1.7; }
h1 { border-bottom: 2px solid #333; padding-bottom: 8px; }
h2 { border-bottom: 1px solid #999; padding-bottom: 4px; margin-top: 28px; }
table { border-collapse: collapse; width: 100%; margin: 12px 0; }
th, td { border: 1px solid #999; padding: 6px 10px; font-size: 14px; }
th { background: #f0f0f0; }
code { background: #f5f5f5; padding: 1px 4px; border-radius: 3px; }
pre { background: #f5f5f5; padding: 10px; overflow-x: auto; }
pre code { background: none; padding: 0; }
blockquote { border-left: 4px solid #ccc; margin: 12px 0; padding: 4px 16px;
             color: #555; background: #fafafa; }
"""

def main():
    md_text = open(MD, encoding="utf-8").read()
    body = markdown.markdown(md_text, extensions=["tables", "fenced_code"])
    html = f"""<!DOCTYPE html><html lang="zh"><head><meta charset="utf-8">
<title>实验报告</title><style>{CSS}</style></head><body>{body}</body></html>"""
    open(HTML, "w", encoding="utf-8").write(html)
    subprocess.run([CHROME, "--headless", "--disable-gpu", "--no-pdf-header-footer",
                    f"--print-to-pdf={PDF}",
                    "file:///" + HTML.replace("\\", "/")],
                   check=True, capture_output=True)
    size = os.path.getsize(PDF)
    print(f"PDF 已生成: {PDF} ({size/1024:.0f} KB)")

if __name__ == "__main__":
    main()
