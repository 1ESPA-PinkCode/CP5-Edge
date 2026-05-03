"""
Dashboard Vinheria Agnello — backend Flask
==========================================
Consome dados do FIWARE (Orion + STH-Comet) e expõe APIs para o dashboard.
Também envia comandos remotos (alert_on, alert_off, silence_buzzer) ao ESP32
via Orion → IoT Agent → MQTT.

Endpoints:
  GET  /                       Dashboard HTML
  GET  /api/current            Estado atual da entidade (Orion :1026)
  GET  /api/history?lastN=N    Histórico dos sensores (STH :8666)
  POST /api/cmd/alert_on       Aciona alerta remoto
  POST /api/cmd/alert_off      Desliga alerta remoto
  POST /api/cmd/silence_buzzer Silencia buzzer
  GET  /api/health             Healthcheck dos serviços FIWARE
"""

from flask import Flask, jsonify, render_template, request
import requests
import logging

# ───── Configuração FIWARE ──────────────────────────────────
# Como o dashboard roda NA MESMA VM do FIWARE, usamos localhost.
# Os containers expõem as portas 1026 (Orion) e 8666 (STH-Comet) no host.
FIWARE_HOST       = "localhost"
ORION_PORT        = 1026
STH_PORT          = 8666

ENTITY_ID         = "urn:ngsi-ld:VinheriaAgnello:001"
ENTITY_TYPE       = "VinheriaAgnello"
FIWARE_SERVICE    = "smart"
FIWARE_SVCPATH    = "/"

FIWARE_HEADERS = {
    "fiware-service": FIWARE_SERVICE,
    "fiware-servicepath": FIWARE_SVCPATH,
}

# Atributos com histórico que vamos puxar do STH-Comet
HISTORY_ATTRS = ["temperature", "humidity", "luminosity"]

# ───── Logging ─────────────────────────────────────────────
logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(message)s",
)
log = logging.getLogger("vinheria-dashboard")

# ───── Flask app ───────────────────────────────────────────
app = Flask(__name__)


# ============================================================
#  Helpers — wrappers das APIs FIWARE
# ============================================================
def orion_get_entity():
    """Retorna a entidade VinheriaAgnello001 inteira do Orion."""
    url = f"http://{FIWARE_HOST}:{ORION_PORT}/v2/entities/{ENTITY_ID}"
    r = requests.get(url, headers=FIWARE_HEADERS, timeout=5)
    r.raise_for_status()
    return r.json()


def orion_send_command(cmd_name):
    """
    Envia um comando para a entidade no Orion.
    Orion repassa ao IoT Agent, que publica via MQTT pro ESP32.
    Retorna 204 No Content em caso de sucesso.
    """
    url = f"http://{FIWARE_HOST}:{ORION_PORT}/v2/entities/{ENTITY_ID}/attrs"
    headers = {**FIWARE_HEADERS, "Content-Type": "application/json"}
    payload = {cmd_name: {"type": "command", "value": ""}}
    r = requests.patch(url, headers=headers, json=payload, timeout=5)
    r.raise_for_status()
    return r.status_code


def sth_get_history(attr, last_n=50):
    """
    Pega últimas N leituras de um atributo no STH-Comet.
    Retorna lista de dicts: [{recvTime, attrValue}, ...]
    """
    url = (
        f"http://{FIWARE_HOST}:{STH_PORT}/STH/v1/contextEntities"
        f"/type/{ENTITY_TYPE}/id/{ENTITY_ID}/attributes/{attr}"
    )
    params = {"lastN": last_n}
    r = requests.get(url, headers=FIWARE_HEADERS, params=params, timeout=10)
    r.raise_for_status()
    data = r.json()

    # Caminho do STH: contextResponses[0].contextElement.attributes[0].values
    try:
        values = (data["contextResponses"][0]
                      ["contextElement"]
                      ["attributes"][0]
                      ["values"])
    except (KeyError, IndexError):
        return []

    # Normaliza pra lista enxuta
    return [
        {"recvTime": v.get("recvTime"), "value": v.get("attrValue")}
        for v in values
    ]


def extract_attr(entity, name, default=None):
    """Pega .value de um atributo da entidade Orion, com fallback."""
    a = entity.get(name)
    if not a:
        return default
    return a.get("value", default)


# ============================================================
#  Rotas
# ============================================================
@app.route("/")
def index():
    """Dashboard HTML."""
    return render_template("index.html")


@app.route("/api/current")
def api_current():
    """Estado atual lido do Orion (single-shot)."""
    try:
        entity = orion_get_entity()
    except requests.exceptions.RequestException as e:
        log.error(f"Falha ao consultar Orion: {e}")
        return jsonify({"error": "orion_unavailable", "detail": str(e)}), 502

    return jsonify({
        "entity_id":         entity.get("id"),
        "entity_type":       entity.get("type"),
        "state":             extract_attr(entity, "state"),
        "temperature":       extract_attr(entity, "temperature"),
        "humidity":          extract_attr(entity, "humidity"),
        "luminosity":        extract_attr(entity, "luminosity"),
        "environmentStatus": extract_attr(entity, "environmentStatus"),
        "comfortLevel":      extract_attr(entity, "comfortLevel"),
        "decisionReason":    extract_attr(entity, "decisionReason"),
        "alertStatus":       extract_attr(entity, "alertStatus"),
        "alertType":         extract_attr(entity, "alertType"),
        "triggerViolated":   extract_attr(entity, "triggerViolated"),
        "blueLedState":      extract_attr(entity, "blueLedState"),
        "buzzerState":       extract_attr(entity, "buzzerState"),
        "thresholds": {
            "temperatureMin": extract_attr(entity, "temperatureMin"),
            "temperatureMax": extract_attr(entity, "temperatureMax"),
            "humidityMin":    extract_attr(entity, "humidityMin"),
            "humidityMax":    extract_attr(entity, "humidityMax"),
            "luminosityMax":  extract_attr(entity, "luminosityMax"),
        },
    })


@app.route("/api/history")
def api_history():
    """Histórico dos sensores via STH-Comet."""
    try:
        last_n = int(request.args.get("lastN", 50))
    except ValueError:
        last_n = 50
    last_n = max(1, min(last_n, 500))  # clamp

    result = {}
    for attr in HISTORY_ATTRS:
        try:
            result[attr] = sth_get_history(attr, last_n)
        except requests.exceptions.RequestException as e:
            log.error(f"Falha ao consultar STH para {attr}: {e}")
            result[attr] = []

    return jsonify({"lastN": last_n, "data": result})


@app.route("/api/cmd/<cmd>", methods=["POST"])
def api_cmd(cmd):
    """Envia um comando remoto pro ESP32 via Orion."""
    if cmd not in {"alert_on", "alert_off", "silence_buzzer"}:
        return jsonify({"error": "unknown_command", "command": cmd}), 400

    try:
        status = orion_send_command(cmd)
    except requests.exceptions.RequestException as e:
        log.error(f"Falha ao enviar comando {cmd}: {e}")
        return jsonify({"error": "orion_unavailable", "detail": str(e)}), 502

    log.info(f"Comando {cmd} enviado (Orion respondeu {status})")
    return jsonify({"command": cmd, "status": "sent", "orion_status": status})


@app.route("/api/health")
def api_health():
    """Healthcheck rápido do Orion + STH."""
    health = {}
    try:
        r = requests.get(f"http://{FIWARE_HOST}:{ORION_PORT}/version", timeout=3)
        health["orion"] = "ok" if r.ok else f"http_{r.status_code}"
    except requests.exceptions.RequestException as e:
        health["orion"] = f"down ({e.__class__.__name__})"

    try:
        r = requests.get(f"http://{FIWARE_HOST}:{STH_PORT}/version", timeout=3)
        health["sth"] = "ok" if r.ok else f"http_{r.status_code}"
    except requests.exceptions.RequestException as e:
        health["sth"] = f"down ({e.__class__.__name__})"

    return jsonify(health)


# ============================================================
#  Main
# ============================================================
if __name__ == "__main__":
    # 0.0.0.0 = aceita conexões de qualquer IP, porta 5000 (requisito do CP5)
    app.run(host="0.0.0.0", port=5000, debug=False)
