"""
Backend do Dashboard - Vinheria Agnello

Responsabilidades:
- Consultar dados atuais no Orion Context Broker
- Recuperar histórico de sensores via STH-Comet
- Enviar comandos remotos ao ESP32 (via IoT Agent e MQTT)

Arquitetura:
Flask → Orion → IoT Agent → MQTT → ESP32
"""

from flask import Flask, jsonify, render_template, request
import requests
import logging

# ───── Configuração FIWARE ──────────────────────────────────
# Como o backend roda na mesma VM dos serviços FIWARE,
# utilizamos localhost para acesso direto aos containers expostos.
FIWARE_HOST       = "localhost"
ORION_PORT        = 1026
STH_PORT          = 8666

# Identificação da entidade monitorada
ENTITY_ID         = "urn:ngsi-ld:VinheriaAgnello:001"
ENTITY_TYPE       = "VinheriaAgnello"

# Cabeçalhos obrigatórios do FIWARE
FIWARE_SERVICE    = "smart"
FIWARE_SVCPATH    = "/"

FIWARE_HEADERS = {
    "fiware-service": FIWARE_SERVICE,
    "fiware-servicepath": FIWARE_SVCPATH,
}

# Atributos que possuem histórico armazenado no STH-Comet
HISTORY_ATTRS = ["temperature", "humidity", "luminosity"]

# ───── Configuração de Logging ─────────────────────────────
# Define formato e nível de logs da aplicação
logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(message)s",
)
log = logging.getLogger("vinheria-dashboard")

# ───── Inicialização do Flask ──────────────────────────────
app = Flask(__name__)


# ============================================================
# Helpers — Integração com APIs do FIWARE
# ============================================================

def orion_get_entity():
    """
    Obtém a entidade completa no Orion Context Broker.

    Returns:
        dict: JSON contendo todos os atributos da entidade

    Raises:
        requests.exceptions.RequestException: erro de conexão ou resposta inválida
    """
    url = f"http://{FIWARE_HOST}:{ORION_PORT}/v2/entities/{ENTITY_ID}"
    r = requests.get(url, headers=FIWARE_HEADERS, timeout=5)
    r.raise_for_status()
    return r.json()


def orion_send_command(cmd_name):
    """
    Envia um comando para a entidade via Orion.

    Fluxo:
    Orion → IoT Agent → MQTT → ESP32

    Args:
        cmd_name (str): nome do comando (ex: alert_on, alert_off)

    Returns:
        int: status HTTP retornado pelo Orion (esperado: 204)
    """
    url = f"http://{FIWARE_HOST}:{ORION_PORT}/v2/entities/{ENTITY_ID}/attrs"
    headers = {**FIWARE_HEADERS, "Content-Type": "application/json"}
    payload = {cmd_name: {"type": "command", "value": ""}}

    r = requests.patch(url, headers=headers, json=payload, timeout=5)
    r.raise_for_status()
    return r.status_code


def sth_get_history(attr, last_n=50):
    """
    Recupera histórico de um atributo via STH-Comet.

    Args:
        attr (str): nome do atributo (temperature, humidity, luminosity)
        last_n (int): quantidade máxima de registros retornados

    Returns:
        list[dict]: lista de medições no formato:
            [{"recvTime": str, "value": any}, ...]
    """
    url = (
        f"http://{FIWARE_HOST}:{STH_PORT}/STH/v1/contextEntities"
        f"/type/{ENTITY_TYPE}/id/{ENTITY_ID}/attributes/{attr}"
    )
    params = {"lastN": last_n}

    r = requests.get(url, headers=FIWARE_HEADERS, params=params, timeout=10)
    r.raise_for_status()
    data = r.json()

    # A resposta do STH-Comet possui estrutura aninhada.
    # Extraímos apenas a lista de valores do atributo solicitado.
    try:
        values = (data["contextResponses"][0]
                      ["contextElement"]
                      ["attributes"][0]
                      ["values"])
    except (KeyError, IndexError):
        return []

    # Normalização dos dados para formato simplificado
    return [
        {"recvTime": v.get("recvTime"), "value": v.get("attrValue")}
        for v in values
    ]


def extract_attr(entity, name, default=None):
    """
    Extrai o valor de um atributo da entidade Orion.

    Args:
        entity (dict): entidade completa retornada pelo Orion
        name (str): nome do atributo
        default: valor padrão caso o atributo não exista

    Returns:
        any: valor do atributo ou default
    """
    a = entity.get(name)
    if not a:
        return default
    return a.get("value", default)


# ============================================================
# Rotas da aplicação
# ============================================================

@app.route("/")
def index():
    """Renderiza a página principal do dashboard."""
    return render_template("index.html")


@app.route("/api/current")
def api_current():
    """
    Retorna o estado atual da entidade consultando o Orion.

    Realiza uma leitura direta (single-shot) dos dados mais recentes.
    """
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
    """
    Retorna o histórico recente dos sensores via STH-Comet.

    Query params:
        lastN (int): quantidade de registros (default=50, máx=500)
    """
    try:
        last_n = int(request.args.get("lastN", 50))
    except ValueError:
        last_n = 50

    # Garante limites seguros para evitar sobrecarga
    last_n = max(1, min(last_n, 500))

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
    """
    Envia um comando remoto ao dispositivo via Orion.

    Comandos suportados:
        - alert_on
        - alert_off
        - silence_buzzer
    """
    if cmd not in {"alert_on", "alert_off", "silence_buzzer"}:
        return jsonify({"error": "unknown_command", "command": cmd}), 400

    try:
        status = orion_send_command(cmd)
    except requests.exceptions.RequestException as e:
        log.error(f"Falha ao enviar comando {cmd}: {e}")
        return jsonify({"error": "orion_unavailable", "detail": str(e)}), 502

    log.info(f"Comando {cmd} enviado (Orion respondeu {status})")

    return jsonify({
        "command": cmd,
        "status": "sent",
        "orion_status": status
    })


@app.route("/api/health")
def api_health():
    """
    Verifica a disponibilidade dos serviços Orion e STH-Comet.

    Returns:
        dict: status de cada serviço (ok ou erro)
    """
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
# Execução da aplicação
# ============================================================

if __name__ == "__main__":
    # 0.0.0.0 permite acesso externo (necessário para testes e integração)
    # Porta 5000 conforme requisito do projeto (CP5)
    app.run(host="0.0.0.0", port=5000, debug=False)