/* DATOVY PARALLELISMUS
 * Pouziti:
 *   sqx <vstupni_soubor>
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
        #pragma omp critical
        {
            if (currentScore > solver.bestScore) {
                solver.bestScore = currentScore;
                solver.bestCellState = state.cellState;
                solver.bestPlacedPieces = state.placedPieces;
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

int solve(Solver& solver) {
    int rows = solver.board.rows;
    int cols = solver.board.cols;

    // Horni odhad = soucet vsech kladnych policek
    int remainingPosSum = 0;
    for (int r = 0; r < rows; r++)
        for (int c = 0; c < cols; c++)
            if (solver.board.cells[r][c] > 0)
                remainingPosSum += solver.board.cells[r][c];

    // Expanze pocatecnich stavu
    int totalCells = rows * cols;
    int expandDepth = std::clamp(totalCells / 4, 2, 32);

    SearchState initState;
    initState.cellState.assign(rows, std::vector<int>(cols, 0));

    // Predgeneravni pocatecnich stavu (rozbal strom do urcite hloubky)
    std::vector<WorkItem> workQueue;
    dfs(solver, initState, 0, 0, remainingPosSum, 0,
        &workQueue, expandDepth);

    std::cout << "Spousteni DFS resice...\n";
    std::cout << "  Pocatecni horni odhad (soucet kladnych): "
              << remainingPosSum << "\n";
    std::cout << "  Pocet vlaken: " << omp_get_max_threads() << "\n";
    std::cout << "  Vygenerovano pocatecnich stavu: "
              << workQueue.size() << "\n";

    auto t0 = std::chrono::steady_clock::now();

    #pragma omp parallel for schedule(dynamic, 1)
    for (int i = 0; i < (int)workQueue.size(); i++) {
        SearchState localState = workQueue[i].state;
        dfs(solver, localState, workQueue[i].startPos,
            workQueue[i].currentScore, workQueue[i].remainingPosSum, 0);
    }

    auto t1 = std::chrono::steady_clock::now();
    solver.elapsedSec = std::chrono::duration<double>(t1 - t0).count();

    std::cout << "DFS dokonceno.\n";
    std::cout << "  Cas reseni: " << solver.elapsedSec << " s\n";
    std::cout << "  Nejlepsi skore: " << solver.bestScore << "\n";
    std::cout << "  Pocet umistenych quatromin: "
              << solver.bestPlacedPieces.size() << "\n";

    return solver.bestScore;
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
    out << "Pocet vlaken: " << omp_get_max_threads() << "\n";
}

int main(int argc, char* argv[]) {
    std::string inputName = argv[1];
    std::string inputPath = "mapb/" + inputName;

    if (argc >= 3) {
        int nThreads = std::stoi(argv[2]);
        omp_set_num_threads(nThreads);
    }

    //Nacteni desky
    Board board;
    try {
        board = loadBoard(inputPath);
    } catch (const std::exception& e) {
        std::cerr << "Chyba: " << e.what() << "\n";
        return 1;
    }

    std::cout << "Nactena deska ze souboru: " << inputPath << "\n";
    printBoard(board);

    int totalSum = 0;
    for (int r = 0; r < board.rows; r++)
        for (int c = 0; c < board.cols; c++)
            totalSum += board.cells[r][c];
    std::cout << "Soucet vsech policek: " << totalSum << "\n\n";


    // Predvypocet vsech moznych umisteni
    std::vector<Piece> allPlacements =
        generateAllPlacements(board, getAllShapes());

    // DFS reseni
    Solver solver;
    initSolver(solver, board, allPlacements);
    int bestScore = solve(solver);

    // Vypis vysledku
    std::cout << "\n========== VYSLEDEK ==========\n";
    std::cout << "Maximalni skore: " << bestScore << "\n";
    std::cout << "Zlepseni pokrytim: " << (bestScore - totalSum) << "\n\n";

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

    std::string outputPath = "mapsol/" + baseName + "_sol_data_par_" + timeStr + ".txt";
    std::ofstream fout(outputPath);
    if (fout.is_open()) {
        writeSolution(fout, board, solver.bestCellState,
                      solver.bestPlacedPieces, bestScore,
                      solver.elapsedSec);
        std::cout << "Reseni ulozeno do: " << outputPath << "\n";
    }

    // Structured output for benchmarking (CSV: variant,input,threads,score,dfs_calls,time_sec)
    std::cout << "BENCH_RESULT,data_paral," << inputName << ","
              << omp_get_max_threads() << ","
              << bestScore << "," << solver.elapsedSec << "\n";

    return 0;
}