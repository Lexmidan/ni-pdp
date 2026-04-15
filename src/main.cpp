/*
 * MPI + OpenMP reseni
 * Pouziti:
 *   mpirun -np <N> sqx <vstupni_soubor> [pocet_omp_vlaken]
 *
 * Vstupni soubor se hleda ve slozce mapb/, vystup se uklada do mapsol/.
 */

#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <algorithm>
#include <ctime>
#include <climits>
#include <chrono>
#include <filesystem>
#include <omp.h>
#include <mpi.h>

// Herni deska – 2D matice ohodnocenych policek.
struct Board {
    int rows;
    int cols;
    std::vector<std::vector<int>> cells;
};


struct Piece {
    char type; // T/L
    std::vector<std::pair<int,int>> cells; // 4 souradnice (radek, sloupec)
};

// Per-task mutable DFS state
struct SearchState {
    std::vector<std::vector<int>> cellState;
    std::vector<Piece> placedPieces;
    int pieceCounter = 0;
};

struct WorkItem {
    SearchState state;
    int startPos;
    int currentScore;
    int remainingPosSum;
};

// MPI tagy
static const int TAG_WORK = 1;
static const int TAG_RESULT = 2;
static const int TAG_TERMINATE = 3;

//  Serializace
std::vector<char> serializeWorkItem(const WorkItem& wi, int rows, int cols, int bestScore) {
    // bestScore | startPos | currentScore | 
    // remainingPosSum | pieceCounter | cellState[rows*cols] | numPieces 
    // | pieces (type + 4x(r,c))
    int numPieces = (int)wi.state.placedPieces.size();
    int intCount = 5 + rows * cols + 1 + numPieces * 9; // 5 for 9 ints per piece: type + 4*(r,c)
    std::vector<char> buf(intCount * sizeof(int));
    int* p = reinterpret_cast<int*>(buf.data());

    *p++ = bestScore;
    *p++ = wi.startPos;
    *p++ = wi.currentScore;
    *p++ = wi.remainingPosSum;
    *p++ = wi.state.pieceCounter;
    for (int r = 0; r < rows; r++)
        for (int c = 0; c < cols; c++)
            *p++ = wi.state.cellState[r][c];
    *p++ = numPieces;
    for (const auto& piece : wi.state.placedPieces) {
        *p++ = (int)piece.type;
        for (const auto& [cr, cc] : piece.cells) {
            *p++ = cr;
            *p++ = cc;
        }
    }
    return buf;
}

WorkItem deserializeWorkItem(const std::vector<char>& buf, int rows, int cols, int& bestScore) {
    const int* p = reinterpret_cast<const int*>(buf.data());
    WorkItem wi;

    bestScore = *p++;
    wi.startPos = *p++;
    wi.currentScore = *p++;
    wi.remainingPosSum = *p++;
    wi.state.pieceCounter = *p++;
    wi.state.cellState.assign(rows, std::vector<int>(cols));
    for (int r = 0; r < rows; r++)
        for (int c = 0; c < cols; c++)
            wi.state.cellState[r][c] = *p++;
    int numPieces = *p++;
    wi.state.placedPieces.resize(numPieces);
    for (int i = 0; i < numPieces; i++) {
        wi.state.placedPieces[i].type = (char)*p++;
        wi.state.placedPieces[i].cells.resize(4);
        for (int j = 0; j < 4; j++) {
            wi.state.placedPieces[i].cells[j].first = *p++;
            wi.state.placedPieces[i].cells[j].second = *p++;
        }
    }
    return wi;
}

// Serializace vysledku: bestScore | numPieces | cellState | pieces
std::vector<char> serializeResult(int score, const std::vector<std::vector<int>>& cellState,
                                   const std::vector<Piece>& pieces,
                                   int rows, int cols) {
    int numPieces = (int)pieces.size();
    int intCount = 2 + rows * cols + numPieces * 9;
    std::vector<char> buf(intCount * sizeof(int));
    int* p = reinterpret_cast<int*>(buf.data());

    *p++ = score;
    *p++ = numPieces;
    for (int r = 0; r < rows; r++)
        for (int c = 0; c < cols; c++)
            *p++ = cellState[r][c];
    for (const auto& piece : pieces) {
        *p++ = (int)piece.type;
        for (const auto& [cr, cc] : piece.cells) {
            *p++ = cr;
            *p++ = cc;
        }
    }
    return buf;
}

void deserializeResult(const std::vector<char>& buf, int rows, int cols,
                       int& score, std::vector<std::vector<int>>& cellState,
                       std::vector<Piece>& pieces) {
    const int* p = reinterpret_cast<const int*>(buf.data());
    score = *p++;
    int numPieces = *p++;
    cellState.assign(rows, std::vector<int>(cols));
    for (int r = 0; r < rows; r++)
        for (int c = 0; c < cols; c++)
            cellState[r][c] = *p++;
    pieces.resize(numPieces);
    for (int i = 0; i < numPieces; i++) {
        pieces[i].type = (char)*p++;
        pieces[i].cells.resize(4);
        for (int j = 0; j < 4; j++) {
            pieces[i].cells[j].first = *p++;
            pieces[i].cells[j].second = *p++;
        }
    }
}

// stav solveru – deska, umisteni, nejlepsi reseni.
struct Solver {
    Board board;
    std::vector<Piece> allPlacements; // vsechna mozna umisteni

    std::vector<std::vector<std::vector<int>>> placementsForCell;

    // Statistiky
    double elapsedSec;

    // Nejlepsi nalezene reseni (sdilene, chranene #pragma omp critical)
    int bestScore;
    std::vector<std::vector<int>> bestCellState;
    std::vector<Piece> bestPlacedPieces;
};

Board loadBoard(const std::string& filepath) {
    std::ifstream fin(filepath);
    if (!fin.is_open()) {
        throw std::runtime_error("Nelze otevrit soubor: " + filepath);
    }

    Board board;
    fin >> board.rows >> board.cols;
    board.cells.resize(board.rows, std::vector<int>(board.cols));

    for (int r = 0; r < board.rows; r++) {
        for (int c = 0; c < board.cols; c++) {
            if (!(fin >> board.cells[r][c])) {
                throw std::runtime_error(
                    "Chyba cteni policka [" + std::to_string(r) + "]["
                    + std::to_string(c) + "]");
            }
        }
    }
    return board;
}

// Vypise desku na konzoli ve formatovanem tvaru.
void printBoard(const Board& board) {

    std::cout << "Deska " << board.rows << " x " << board.cols << ":\n";
    for (int r = 0; r < board.rows; r++) {
        for (int c = 0; c < board.cols; c++) {
            std::printf("%5d", board.cells[r][c]);
        }
        std::cout << "\n";
    }
}

// Generuje vsechny tvary
std::vector<Piece> getAllShapes() {
    return {
        // T – 4 rotace
        {'T', {{0,1},{1,0},{1,1},{1,2}}},
        {'T', {{0,0},{1,0},{1,1},{2,0}}},
        {'T', {{0,0},{0,1},{0,2},{1,1}}},
        {'T', {{0,1},{1,0},{1,1},{2,1}}},

        // L – 4 rotace
        {'L', {{0,0},{1,0},{2,0},{2,1}}},
        {'L', {{0,0},{0,1},{0,2},{1,0}}},
        {'L', {{0,0},{0,1},{1,1},{2,1}}},
        {'L', {{0,2},{1,0},{1,1},{1,2}}},

        // L zrcadlene (= tvar J) – 4 rotace
        {'L', {{0,1},{1,1},{2,0},{2,1}}},
        {'L', {{0,0},{1,0},{1,1},{1,2}}},
        {'L', {{0,0},{0,1},{1,0},{2,0}}},
        {'L', {{0,0},{0,1},{0,2},{1,2}}},
    };
}



// Pro kazdy tvar a kazdou pozici na desce zkontrolujeme, zda se tam vejde.
// Pokud ano, ulozime absolutni souradnice 4 pokrytych policek.
std::vector<Piece> generateAllPlacements(const Board& board,
                                         const std::vector<Piece>& shapes) {

    std::vector<Piece> placements;

    for (const auto& shape : shapes) {
        // Zjistime maximalní radek a sloupec tvaru (velikost bounding boxu)
        int maxR = 0, maxC = 0;
        for (const auto& [sr, sc] : shape.cells) {
            maxR = std::max(maxR, sr);
            maxC = std::max(maxC, sc);
        }

        // Zkousime umistit levy horni roh tvaru na kazdou pozici,
        // kde se jeste cely tvar vejde do desky
        for (int r = 0; r + maxR < board.rows; r++) {
            for (int c = 0; c + maxC < board.cols; c++) {
                Piece pl;
                pl.type = shape.type;
                int coveredSum = 0;
                for (const auto& [sr, sc] : shape.cells) {
                    pl.cells.push_back({r + sr, c + sc});
                    coveredSum += board.cells[r + sr][c + sc];
                }
                // Pokrytim bunek se souctem >= 0 se skore nikdy nezlepsi
                if (coveredSum < 0) {
                    placements.push_back(pl);
                }
            }
        }
    }
    return placements;
}


// Pripravi datove struktury a pro kazde policko predpocita, ktera umisteni zacinaji na tomto policku.
void initSolver(Solver& solver, const Board& board,

    const std::vector<Piece>& placements) {
    solver.board = board;
    solver.allPlacements = placements;
    solver.bestScore = INT_MIN;
    solver.bestCellState.assign(board.rows, std::vector<int>(board.cols, 0));

    solver.placementsForCell.assign(board.rows,
        std::vector<std::vector<int>>(board.cols));

    //predpocitame sumu pokrytych bunek pro kazde umisteni
    std::vector<int> placementSum(placements.size());
    for (int i = 0; i < (int)placements.size(); i++) {
        int s = 0;
        for (const auto& [cr, cc] : placements[i].cells) {
            s += board.cells[cr][cc];
        }
        placementSum[i] = s;
    }

    for (int i = 0; i < (int)placements.size(); i++) {
        // Najdeme bunku s nejmensi linearni pozici (= prvni v poradi skenovani)
        int minPos = board.rows * board.cols;
        int minR = -1, minC = -1;
        for (const auto& [cr, cc] : placements[i].cells) {
            int pos = cr * board.cols + cc;
            if (pos < minPos) {
                minPos = pos;
                minR = cr;
                minC = cc;
            }
        }
        solver.placementsForCell[minR][minC].push_back(i);
    }

    //Seradime umisteni pro kazde policko podle sumy (nejzapornejsi prvni)
    for (int r = 0; r < board.rows; r++) {
        for (int c = 0; c < board.cols; c++) {
            auto& vec = solver.placementsForCell[r][c];
            std::sort(vec.begin(), vec.end(), [&](int a, int b) {
                return placementSum[a] < placementSum[b];
            });
        }
    }
}


// DFS prohledavani. Pokud je workQueue != nullptr a depth < maxExpandDepth, 
// misto rekurze sbira stavy do fronty pro datovy paralelismus.
void dfs(Solver& solver, SearchState& state, int startPos,
         int currentScore, int remainingPosSum, int depth,
         std::vector<WorkItem>* workQueue = nullptr, int maxExpandDepth = 0) {

    int cols = solver.board.cols;
    int totalCells = solver.board.rows * cols;

    // Najdi dalsi volne policko (preskoc rozhodnuta)
    while (startPos < totalCells) {
        int r = startPos / cols;
        int c = startPos % cols;
        if (state.cellState[r][c] == 0) break;
        startPos++;
    }

    // Generovani work items: dosazena cilova hloubka nebo konec desky
    if (workQueue && (startPos >= totalCells || depth >= maxExpandDepth)) {
        workQueue->push_back({state, startPos, currentScore, remainingPosSum});
        return;
    }

    // Vsechna policka rozhodnuta => koncovy stav
    if (startPos >= totalCells) {
        if (currentScore > solver.bestScore) {
            #pragma omp critical
            {
                if (currentScore > solver.bestScore) {
                    solver.bestScore = currentScore;
                    solver.bestCellState = state.cellState;
                    solver.bestPlacedPieces = state.placedPieces;
                }
            }
        }
        return;
    }

    int r = startPos / cols;
    int c = startPos % cols;

    // Orezavani: optimisticky odhad nemuze prekonat nejlepsi reseni
    if (currentScore + remainingPosSum <= solver.bestScore) {
        return;
    }

    //A: Umistit quatromino pokryvajici policko (r,c)
    for (int plIdx : solver.placementsForCell[r][c]) {
        const Piece& pl = solver.allPlacements[plIdx];

        // Zkontroluj, zda jsou vsechny 4 bunky volne
        bool canPlace = true;
        for (const auto& [cr, cc] : pl.cells) {
            if (state.cellState[cr][cc] != 0) { canPlace = false; break; }
        }
        if (!canPlace) continue;

        // Umisti kus (oznac 4 bunky jeho ID)
        state.pieceCounter++;
        int coveredPosSum = 0;
        for (const auto& [cr, cc] : pl.cells) {
            state.cellState[cr][cc] = state.pieceCounter;
            if (solver.board.cells[cr][cc] > 0) {
                coveredPosSum += solver.board.cells[cr][cc];
            }
        }
        state.placedPieces.push_back(pl);

        dfs(solver, state, startPos + 1, currentScore,
            remainingPosSum - coveredPosSum, depth + 1,
            workQueue, maxExpandDepth);

        //undo
        state.placedPieces.pop_back();
        for (const auto& [cr, cc] : pl.cells) {
            state.cellState[cr][cc] = 0;
        }
        state.pieceCounter--;
    }

    //B: Preskocit policko (nechat nepokryte) – posledni vetev, vzdy inline
    state.cellState[r][c] = -1;
    int val = solver.board.cells[r][c];
    int posDelta = (val > 0) ? val : 0;

    dfs(solver, state, startPos + 1, currentScore + val,
        remainingPosSum - posDelta, depth + 1,
        workQueue, maxExpandDepth);

    state.cellState[r][c] = 0;   // backtracking
}

// Slave: lokalni DFS reseni jednoho WorkItemu s OpenMP
void solveLocal(Solver& solver, WorkItem& wi) {

    // Rozbalime WorkItem do sub-uloh pro OMP paralelismus
    int totalCells = solver.board.rows * solver.board.cols;
    int subDepth = std::clamp(totalCells / 6, 2, 16);

    std::vector<WorkItem> subQueue;
    SearchState tmpState = wi.state;
    dfs(solver, tmpState, wi.startPos, wi.currentScore,
        wi.remainingPosSum, 0, &subQueue, subDepth);

    if (subQueue.empty()) return;

    #pragma omp parallel for schedule(dynamic, 1)
    for (int i = 0; i < (int)subQueue.size(); i++) {
        SearchState localState = subQueue[i].state;
        dfs(solver, localState, subQueue[i].startPos,
            subQueue[i].currentScore, subQueue[i].remainingPosSum, 0);
    }
}

// MASTER
int solveMaster(Solver& solver, int numProcs) {
    int rows = solver.board.rows;
    int cols = solver.board.cols;

    // Horni odhad = soucet vsech kladnych policek
    int remainingPosSum = 0;
    for (int r = 0; r < rows; r++)
        for (int c = 0; c < cols; c++)
            if (solver.board.cells[r][c] > 0)
                remainingPosSum += solver.board.cells[r][c];

    // Expanze pocatecnich stavu – zvysujeme hloubku, dokud nemame dost work items
    int totalCells = rows * cols;
    int expandDepth = std::clamp(totalCells / 4, 2, 32);
    int minWorkItems = numProcs * 16; // chceme alespon 16x vice work items nez procesu

    SearchState initState;
    initState.cellState.assign(rows, std::vector<int>(cols, 0));

    std::vector<WorkItem> workQueue;
    while (expandDepth <= totalCells) {
        workQueue.clear();
        solver.bestScore = INT_MIN; // reset pro novou expanzi
        SearchState tmpState = initState;
        dfs(solver, tmpState, 0, 0, remainingPosSum, 0, &workQueue, expandDepth);
        if ((int)workQueue.size() >= minWorkItems) break;
        expandDepth += std::max(1, (totalCells - expandDepth) / 2);
    }

    // Seradime work items podle potencialu (nejlepsi prvni) pro lepsi orezavani
    std::sort(workQueue.begin(), workQueue.end(), [](const WorkItem& a, const WorkItem& b) {
        return (a.currentScore + a.remainingPosSum) > (b.currentScore + b.remainingPosSum);
    });

    // std::cout << "Spousteni MPI Master-Slave resice...\n";
    // std::cout << "  Pocatecni horni odhad (soucet kladnych): " << remainingPosSum << "\n";
    // std::cout << "  Pocet MPI procesu: " << numProcs << "\n";
    // std::cout << "  Pocet OMP vlaken: " << omp_get_max_threads() << "\n";
    // std::cout << "  Vygenerovano pocatecnich stavu: " << workQueue.size() << "\n";

    auto t0 = std::chrono::steady_clock::now();

    int numSlaves = numProcs - 1;
    int nextWork = 0;
    int activeSlaves = 0; // sleduj kolik maka

    // Pocatecni rozeslani prace
    for (int slave = 1; slave <= numSlaves && nextWork < (int)workQueue.size(); slave++, nextWork++) {
        auto buf = serializeWorkItem(workQueue[nextWork], rows, cols, solver.bestScore);
        int sz = (int)buf.size();
        MPI_Send(&sz, 1, MPI_INT, slave, TAG_WORK, MPI_COMM_WORLD);
        MPI_Send(buf.data(), sz, MPI_BYTE, slave, TAG_WORK, MPI_COMM_WORLD);
        activeSlaves++;
    }

    // // !!! Propustit slave, ktere nedostal zadnou praci - jinak by cekali na praci, ktera neprijde a nedoslo by k ukonceni
    // for (int slave = activeSlaves + 1; slave <= numSlaves; slave++) {
    //     int dummy = 0;
    //     MPI_Send(&dummy, 1, MPI_INT, slave, TAG_TERMINATE, MPI_COMM_WORLD);
    // }

    // Prijimani vysledku a rozesilani dalsi prace
    while (activeSlaves > 0) {
        MPI_Status status;
        int resSz;
        MPI_Recv(&resSz, 1, MPI_INT, MPI_ANY_SOURCE, TAG_RESULT, MPI_COMM_WORLD, &status);
        int slave = status.MPI_SOURCE; // kdo skoncil?

        std::vector<char> resBuf(resSz);
        MPI_Recv(resBuf.data(), resSz, MPI_BYTE, slave, TAG_RESULT, MPI_COMM_WORLD, &status);

        int slaveScore;
        std::vector<std::vector<int>> slaveCellState;
        std::vector<Piece> slavePieces;
        deserializeResult(resBuf, rows, cols, slaveScore, slaveCellState, slavePieces);

        if (slaveScore > solver.bestScore) {
            solver.bestScore = slaveScore;
            solver.bestCellState = slaveCellState;
            solver.bestPlacedPieces = slavePieces;
        }

        // !!! Preskoc work items, ktere uz nemohou zlepsit nejlepsi reseni
        while (nextWork < (int)workQueue.size() &&
               workQueue[nextWork].currentScore + workQueue[nextWork].remainingPosSum <= solver.bestScore) {
            nextWork++;
        }

        // Poslat dalsi praci nebo propustit
        if (nextWork < (int)workQueue.size()) {
            auto buf = serializeWorkItem(workQueue[nextWork], rows, cols, solver.bestScore);
            int sz = (int)buf.size();
            MPI_Send(&sz, 1, MPI_INT, slave, TAG_WORK, MPI_COMM_WORLD);
            MPI_Send(buf.data(), sz, MPI_BYTE, slave, TAG_WORK, MPI_COMM_WORLD);
            nextWork++;
        } else {
            int dummy = 0;
            MPI_Send(&dummy, 1, MPI_INT, slave, TAG_TERMINATE, MPI_COMM_WORLD);
            activeSlaves--;
        }
    }

    auto t1 = std::chrono::steady_clock::now();
    solver.elapsedSec = std::chrono::duration<double>(t1 - t0).count();

    std::cout << "DFS dokonceno.\n";
    std::cout << "  Cas reseni: " << solver.elapsedSec << " s\n";
    std::cout << "  Nejlepsi skore: " << solver.bestScore << "\n";
    std::cout << "  Pocet umistenych quatromin: " << solver.bestPlacedPieces.size() << "\n";

    return solver.bestScore;
}

// SLAVE
void runSlave(Solver& solver) {
    int rows = solver.board.rows;
    int cols = solver.board.cols;

    while (true) {
        MPI_Status status;
        int sz;
        MPI_Recv(&sz, 1, MPI_INT, 0, MPI_ANY_TAG, MPI_COMM_WORLD, &status);

        if (status.MPI_TAG == TAG_TERMINATE) break;

        std::vector<char> buf(sz);
        MPI_Recv(buf.data(), sz, MPI_BYTE, 0, TAG_WORK, MPI_COMM_WORLD, &status);

        int masterBest;
        WorkItem wi = deserializeWorkItem(buf, rows, cols, masterBest);

        // Aktualizuj lokalni best z mastera
        if (masterBest > solver.bestScore) {
            solver.bestScore = masterBest;
        }

        solveLocal(solver, wi);

        // Posli vysledek zpet
        auto resBuf = serializeResult(solver.bestScore, solver.bestCellState,
                                       solver.bestPlacedPieces,
                                       rows, cols);
        int resSz = (int)resBuf.size();
        MPI_Send(&resSz, 1, MPI_INT, 0, TAG_RESULT, MPI_COMM_WORLD);
        MPI_Send(resBuf.data(), resSz, MPI_BYTE, 0, TAG_RESULT, MPI_COMM_WORLD);
    }
}

// Vystup reseni
void writeSolution(std::ostream& out, const Board& board,
                   const std::vector<std::vector<int>>& cellState,
                   const std::vector<Piece>& placedPieces, int score,
                   double elapsedSec) {

    // Prirad label kazdemu kusu (T1, T2, L1, L2, ...)
    int tCount = 0, lCount = 0;
    std::vector<std::string> labels(placedPieces.size());
    for (int i = 0; i < (int)placedPieces.size(); i++) {
        if (placedPieces[i].type == 'T') {
            labels[i] = "T" + std::to_string(++tCount);
        } else {
            labels[i] = "L" + std::to_string(++lCount);
        }
    }

    // Zjisti sirku sloupce pro zarovnani
    int w = 1;
    for (const auto& l : labels) w = std::max(w, (int)l.size());
    for (int r = 0; r < board.rows; r++)
        for (int c = 0; c < board.cols; c++)
            w = std::max(w, (int)std::to_string(board.cells[r][c]).size());
    w += 1;

    // Zapis matice
    for (int r = 0; r < board.rows; r++) {
        for (int c = 0; c < board.cols; c++) {
            std::string text;
            if (cellState[r][c] > 0) {
                text = labels[cellState[r][c] - 1];
            } else {
                text = std::to_string(board.cells[r][c]);
            }
            // Zarovnani doprava
            for (int p = 0; p < w - (int)text.size(); p++) out << ' ';
            out << text;
        }
        out << "\n";
    }

    out << "\nSkore (soucet nepokrytych): " << score << "\n";
    out << "Pocet umistenych quatromin: " << placedPieces.size()
        << " (T: " << tCount << ", L: " << lCount << ")\n";
    out << "Cas reseni: " << elapsedSec << " s\n";
    int mpiSize = 1;
    MPI_Comm_size(MPI_COMM_WORLD, &mpiSize);
    out << "Pocet MPI procesu: " << mpiSize << "\n";
    out << "Pocet OMP vlaken: " << omp_get_max_threads() << "\n";
}

int main(int argc, char* argv[]) {
    MPI_Init(&argc, &argv);

    int rank, numProcs;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank); //0 - master, ostatni slave
    MPI_Comm_size(MPI_COMM_WORLD, &numProcs); // 4

    std::string inputName = argv[1];
    std::string inputPath = "mapb/" + inputName;

    if (argc >= 3) {
        int nThreads = std::stoi(argv[2]);
        omp_set_num_threads(nThreads);
    }

    // Vsechny procesy nacitaji desku a pripravuji solver
    Board board;
    try {
        board = loadBoard(inputPath);
    } catch (const std::exception& e) {
        std::cerr << "Chyba: " << e.what() << "\n";
        MPI_Finalize();
        return 1;
    }

    std::vector<Piece> allPlacements = generateAllPlacements(board, getAllShapes()); // TODO: Mohl by se delat jen v masteru a posilat slaveum jen relevantni cast?
    Solver solver;
    initSolver(solver, board, allPlacements);

    if (rank == 0) {
        // MASTER 
        // std::cout << "Nactena deska ze souboru: " << inputPath << "\n";
        // printBoard(board);

        int totalSum = 0;
        for (int r = 0; r < board.rows; r++)
            for (int c = 0; c < board.cols; c++)
                totalSum += board.cells[r][c];
        std::cout << "Soucet vsech policek: " << totalSum << "\n\n";

        int bestScore = solveMaster(solver, numProcs);

        // std::cout << "\n!!! VYSLEDEK !!!\n";
        // std::cout << "Maximalni skore: " << bestScore << "\n";
        // std::cout << "Zlepseni pokrytim: " << (bestScore - totalSum) << "\n\n";

        writeSolution(std::cout, board, solver.bestCellState,
                      solver.bestPlacedPieces, bestScore,
                      solver.elapsedSec);

        // Ulozeni do souboru
        std::filesystem::create_directories("mapsol");
        std::time_t now = std::time(nullptr);
        char timeStr[64];
        std::strftime(timeStr, sizeof(timeStr), "%Y%m%d_%H%M%S",
                      std::localtime(&now));

        std::string baseName = inputName;
        size_t dot = baseName.rfind('.');
        if (dot != std::string::npos) baseName = baseName.substr(0, dot);

        std::string outputPath = "mapsol/" + baseName + "_sol_mpi_" + timeStr + ".txt";
        std::ofstream fout(outputPath);
        if (fout.is_open()) {
            writeSolution(fout, board, solver.bestCellState,
                          solver.bestPlacedPieces, bestScore,
                          solver.elapsedSec);
            std::cout << "Reseni ulozeno do: " << outputPath << "\n";
        }

        std::cout << "BENCH_RESULT,mpi," << inputName << ","
                  << omp_get_max_threads() << "," << bestScore << ","
                  << solver.elapsedSec << "\n";
    } else {
        //  SLAVE 
        runSlave(solver);
    }

    MPI_Finalize();
    return 0;
}