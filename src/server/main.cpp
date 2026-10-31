// LBN Coop SA dedicated server (original code).
// Usage: LbnCoopSA-Server [--port 7810] [--max 5] [--name "LBN Coop SA"] [--timeout 10000] [--duration-sec N]
#include "../common/log.h"
#include "../common/net_server.h"
#include "../common/version.h"
#include <atomic>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include "../common/net_common.h"

static std::atomic<bool> g_quit{false};
static void onSignal(int) { g_quit = true; }

int main(int argc, char** argv) {
    lbn::ServerOptions opt;
    long durationSec = 0;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&](const char* name) -> const char* {
            if (i + 1 >= argc) { std::fprintf(stderr, "faltou valor para %s\n", name); std::exit(2); }
            return argv[++i];
        };
        if (a == "--port") opt.port = uint16_t(std::atoi(next("--port")));
        else if (a == "--max") opt.maxPlayers = std::atoi(next("--max"));
        else if (a == "--name") opt.name = next("--name");
        else if (a == "--bind") opt.bindAddress = next("--bind");
        else if (a == "--timeout") opt.timeoutMs = uint32_t(std::atoi(next("--timeout")));
        else if (a == "--duration-sec") durationSec = std::atol(next("--duration-sec"));
        else if (a == "-h" || a == "--help") {
            std::printf("%s %s - servidor dedicado\n"
                        "  --port N        porta UDP (padrao %d)\n"
                        "  --max N         jogadores (1-%d)\n"
                        "  --name TEXTO    nome do servidor\n"
                        "  --bind IP       IP local para escutar (padrao: todos)\n"
                        "  --timeout MS    tempo ate derrubar jogador sem resposta\n",
                        LBN_COOP_NAME, LBN_COOP_VERSION, LBN_DEFAULT_PORT, LBN_MAX_PLAYERS);
            return 0;
        }
    }
    lbn::logOpen("LbnCoopSA-Server.log", true);
    LOGI("%s %s (protocolo %d) - servidor dedicado", LBN_COOP_NAME, LBN_COOP_VERSION, LBN_PROTOCOL_VERSION);
    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);

    lbn::Server server;
    if (!server.start(opt)) return 1;
    uint32_t start = lbn::nowMs();
    while (!g_quit) {
        server.service(10);
        if (durationSec > 0 && lbn::nowMs() - start > uint32_t(durationSec * 1000)) break;
    }
    server.stop();
    lbn::logClose();
    return 0;
}
