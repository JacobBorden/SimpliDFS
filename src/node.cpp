#include "node.h"

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <NodeName> <Port>" << std::endl;
        return 1;
    }

    std::string nodeName = argv[1];
    int port = std::stoi(argv[2]);

    Node node(nodeName, port);
    node.start();

    // Keep the main thread running indefinitely
    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    return 0;
}
