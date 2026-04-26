import os
import google.generativeai as genai
from flask import Flask, request, jsonify

app = Flask(__name__)


@app.route('/')
def home():
    return "Crash Guard is live!"


# Configure Gemini API
genai.configure(api_key=os.environ["CrashGuardApi"])
model = genai.GenerativeModel("gemini-2.5-flash-lite")

# Load knowledge base
KNOWLEDGE_BASE = ""
if os.path.exists("app_knowledge.txt"):
    with open("app_knowledge.txt", "r", encoding="utf-8") as f:
        KNOWLEDGE_BASE = f.read()


@app.route("/chat", methods=["GET"])
def chat():
    user_message = request.args.get("message", "").strip()
    if not user_message:
        return jsonify({"error": "No message provided"}), 400

    prompt = f"""
   you are an AI assistant for my Smart Crash Detection System project(CrashGuard).(dont introduce yourself unless asked). 
  Always give clear, concise, and relevant answers. Be more frendly.
  Do not add extra information beyond what is asked. Stick strictly to the knowledge base when describing the project or its features.If ques asked is not relevant to app features or entirely different topic,then answer on your own accodingly(but to try to divert the ans towards my app if its possible )
  Your main goal is to guide users on the aftermath of a crash, including:
  - What to do next for personal safety
  - Health and vehicle insurance guidance from suggesting the best hospitals and isurance schmes
  - How to deal with police or any other legal issues relevant to the crash.
  b
    Project Information:
    {KNOWLEDGE_BASE}
    Users's Question: {user_message}
    Answer:
    """

    # Generate response
    response = model.generate_content(
        prompt,
        generation_config=genai.types.GenerationConfig(temperature=0.8,
                                                       top_p=0.95,
                                                       max_output_tokens=500))

    try:
        final_text = response.text.strip()
        # Optional: replace line breaks with spaces
        final_text = final_text.replace("\n",
                                        " ").replace("\r",
                                                     " ").replace('"', "'")
    except Exception:
        final_text = "Sorry, I couldn't generate a response."

    # Return full response as JSON
    return jsonify({"answer": final_text})


if __name__ == "__main__":
    app.run(host="0.0.0.0", port=8000)
