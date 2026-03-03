/**
 * Skladani quatromin s maximem 
 *
 * Program resi ulohu umistovani quatromin tvaru T a L na obdelnikovou
 * herni desku tak, aby soucet ohodnoceni nepokrytych policek byl maximalni.
 *
 * Pouziti:
 *   sqx <vstupni_soubor>
 *
 * Vstupni soubor se hleda ve slozce mapb/, vystup se uklada do mapsol/.
 *
 * Algoritmus: prohledavani do hloubky (DFS) s orezavanim (branch & bound).
 */

#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <cstdlib>
#include <algorithm>  // sort, max, min
#include <ctime>      // casove razitko pro nazvy vystupnich souboru
#include <climits>    // INT_MIN – minimalni hodnota int
#include <filesystem> // vytvareni adresaru (C++17)


// ---------------------------------------------------------------------------
// Datove struktury
// ---------------------------------------------------------------------------

// Bunka (policko) – dvojice souradnic (radek, sloupec).
// Pouziva se jak pro relativni offsety (v definici tvaru),
// tak pro absolutni pozice na desce.
struct Cell {
    int row;
    int col;
};

// Herni deska – obdelnikova matice ohodnocenych policek
struct Board {
    int rows;                              // pocet radku (a)
    int cols;                              // pocet sloupcu (b)
    std::vector<std::vector<int>> cells;   // ohodnoceni policek [radek][sloupec]
};

// Tvar quatromina – 4 relativni offsety vuci levemu hornimu rohu (0,0).
// Typ urcuje, zda jde o quatromino 'T' nebo 'L'.
// Kazda orientace (rotace/zrcadleni) se uklada jako samostatny Shape.
struct Shape {
    char type;                  // 'T' nebo 'L'
    std::vector<Cell> cells;    // 4 bunky (relativni offsety)
};

// Konkretni umisteni quatromina na desce.
// Obsahuje absolutni souradnice 4 pokrytych policek.
struct Placement {
    char type;                  // 'T' nebo 'L'
    int orientationId;          // index orientace
    std::vector<Cell> cells;    // 4 bunky (absolutni souradnice na desce)
};

Board loadBoard(const std::string& filepath) {
    std::ifstream fin(filepath);
    if (!fin.is_open()) {
        throw std::runtime_error("Nelze otevrit soubor: " + filepath);
    }

    Board board;
    fin >> board.rows >> board.cols;

    board.cells.resize(board.rows, std::vector<int>(board.cols));

    for (int r = 0; r < board.rows; ++r) {
        for (int c = 0; c < board.cols; ++c) {
            if (!(fin >> board.cells[r][c])) {
                throw std::runtime_error(
                    "Chyba cteni policka [" + std::to_string(r) + "][" +
                    std::to_string(c) + "]");
            }
        }
    }

    fin.close();
    return board;
}

void printBoard(const Board& board) {
    std::cout << "Deska " << board.rows << " x " << board.cols << ":\n";
    for (int r = 0; r < board.rows; ++r) {
        for (int c = 0; c < board.cols; ++c) {
            std::printf("%5d", board.cells[r][c]);
        }
        std::cout << "\n";
    }
}


// Kazdy tvar je seznam 4 relativnich offsetu bunek vuci pozici [0,0].
// Bunky jsou serazeny zleva doprava, shora dolu.
std::vector<Shape> makeShapesT() {
    return {
        {'T', {{0,1},{1,0},{1,1},{1,2}}},
        {'T', {{0,0},{1,0},{1,1},{2,0}}},
        {'T', {{0,0},{0,1},{0,2},{1,1}}},
        {'T', {{0,1},{1,0},{1,1},{2,1}}},
    };
}

std::vector<Shape> makeShapesL() {
    return {
        {'L', {{0,0},{1,0},{2,0},{2,1}}},
        {'L', {{0,0},{0,1},{0,2},{1,0}}},
        {'L', {{0,0},{0,1},{1,1},{2,1}}},
        {'L', {{0,2},{1,0},{1,1},{1,2}}},
        {'L', {{0,1},{1,1},{2,0},{2,1}}},
        {'L', {{0,0},{1,0},{1,1},{1,2}}},
        {'L', {{0,0},{0,1},{1,0},{2,0}}},
        {'L', {{0,0},{0,1},{0,2},{1,2}}},
    };
}

// ---------------------------------------------------------------------------
// Generovani umisteni (placements) na desce
// ---------------------------------------------------------------------------

// Pro kazdou orientaci (tvar) zkousime umistit jeho levy horni roh
// na kazdou pozici desky [r][c] a overime, zda vsechny 4 bunky
// tvaru padu do hranic desky.
//
// Vraci vektor vsech validnich umisteni (kazde = absolutni souradnice
// 4 pokrytych policek).
std::vector<Placement> generateAllPlacements(
        const Board& board,
        const std::vector<Shape>& orientations) {

    std::vector<Placement> placements;

    for (int oi = 0; oi < (int)orientations.size(); ++oi) {
        const Shape& shape = orientations[oi];

        // Zjistime velikost ohranicujiciho obdelniku tvaru
        int maxRow = 0, maxCol = 0;
        for (const auto& c : shape.cells) {
            maxRow = std::max(maxRow, c.row);
            maxCol = std::max(maxCol, c.col);
        }

        // Zkousime umistit tvar na kazdou pozici desky.
        // Staci projit pozice, kde se cely tvar vejde do desky:
        //   radek r: 0 .. (rows - 1 - maxRow)
        //   sloupec c: 0 .. (cols - 1 - maxCol)
        for (int r = 0; r + maxRow < board.rows; ++r) {
            for (int c = 0; c + maxCol < board.cols; ++c) {
                Placement pl;
                pl.type = shape.type;
                pl.orientationId = oi;
                for (const auto& cell : shape.cells) {
                    pl.cells.push_back({r + cell.row, c + cell.col});
                }
                placements.push_back(pl);
            }
        }
    }

    return placements;
}



// ---------------------------------------------------------------------------
// DFS resic (branch & bound)
// ---------------------------------------------------------------------------
//
// Hlavni myslenka algoritmu:
//   Prochazime policka desky v poradi "zleva doprava, shora dolu".
//   Pro kazde volne policko mame dve moznosti:
//     a) Umistit na nej quatromino (jedno z kandidatu pokryvajicich toto policko).
//     b) Preskocit ho (ponechat nepokryte).
//
//   Prvni volne policko je vzdy "rozhodovaci bod" – tim zamezime duplicitnimu
//   zkoumani stejnych konfiguraci.
//
//   Pro orezavani (branch & bound) pouzivame horni odhad:
//     upperBound = currentScore + soucet kladnych hodnot zbylych volnych policek
//   Pokud ani tento optimisticky odhad neprekonava dosud nejlepsi reseni,
//   celou vetev orezneme.

// Informace o jednom umistenem quatrominu na desce.
// Uklada typ ('T' nebo 'L') a absolutni souradnice 4 bunek.
struct PlacedPiece {
    char type;                   // 'T' nebo 'L'
    std::vector<Cell> cells;     // 4 absolutni pozice na desce
};

// Stavova struktura DFS resice.
// Uchovava vsechna data potrebna behem rekurzivniho prohledavani.
struct SolverState {
    const Board* board;                         // ukazatel na herni desku
    const std::vector<Placement>* placements;   // vsechna mozna umisteni

    // Pro kazde policko [r][c] seznam indexu do 'placements',
    // kde dane policko je PRVNI bunkou umisteni v poradi skenovani
    // (zleva doprava, shora dolu). Tim zajistime, ze kazde umisteni
    // je zkouseno prave jednou – kdyz narazime na jeho prvni bunku.
    //
    std::vector<std::vector<std::vector<int>>> placementsForCell;

    // Stav kazdeho policka behem DFS:
    //   0  = volne (nerozhodnute)
    //   -1 = preskocene (rozhodnute jako nepokryte)
    //   >0 = pokryte quatrominem s danym ID (1, 2, 3, ...)
    std::vector<std::vector<int>> cellState;

    // Seznam aktualne umistenych quatromin (v poradi umisteni).
    // Pri backtracku se posledni prvek odebira (jako stack/zasobnik).
    std::vector<PlacedPiece> placedPieces;
    int pieceCounter;      // pocitadlo pro prirazovani unikatnich ID

    // Nejlepsi nalezene reseni (aktualizuje se pri kazdem zlepseni)
    int bestScore;
    std::vector<std::vector<int>> bestCellState;
    std::vector<PlacedPiece> bestPlacedPieces;

    // Statistiky
    long long nodesVisited;   // pocet navstivenych uzlu DFS stromu
};

// Inicializuje stav resice.
// Pripravi datove struktury a precompute seznam umisteni pro kazde policko.
//
// Klicova optimalizace: kazde umisteni zaradime pouze k tomu policku,
// ktere je jeho PRVNI bunkou v poradi skenovani. Kdyz DFS narazi na
// volne policko (r,c), zkusi jen umisteni kde (r,c) je ta prvni bunka.
// Vsechny starsi bunky umisteni uz musi byt rozhodnute (pokryte/preskocene),
// takze by umisteni pres ne nebylo platne.
void initSolver(SolverState& state, const Board& board,
                const std::vector<Placement>& placements) {
    state.board = &board;
    state.placements = &placements;
    state.pieceCounter = 0;
    state.bestScore = INT_MIN;     // zaciname s nejhorsi moznou hodnotou
    state.nodesVisited = 0;

    // Vsechna policka zacinaji jako volna (0)
    state.cellState.assign(board.rows, std::vector<int>(board.cols, 0));

    // Precompute: pro kazde policko najdeme umisteni, kde je toto
    // policko PRVNI v poradi skenovani (linearni pozice = row * cols + col).
    state.placementsForCell.assign(board.rows,
        std::vector<std::vector<int>>(board.cols));

    for (int i = 0; i < (int)placements.size(); i++) {
        // Najdeme bunku s nejnizsi linearni pozici v tomto umisteni
        int minPos = board.rows * board.cols;  // sentinel (vysoka hodnota)
        int minR = -1, minC = -1;
        for (const auto& cell : placements[i].cells) {
            int pos = cell.row * board.cols + cell.col;
            if (pos < minPos) {
                minPos = pos;
                minR = cell.row;
                minC = cell.col;
            }
        }
        // Zaradime umisteni k jeho prvni bunce
        state.placementsForCell[minR][minC].push_back(i);
    }
}

// Rekurzivni DFS funkce s orezavanim (branch & bound).
//
// Parametry:
//   state           – stavova struktura resice (modifikovana na miste, backtracking)
//   startPos        – linearni pozice (row * cols + col) od ktere hledame
//                     dalsi volne policko
//   currentScore    – soucet hodnot vsech dosud preskockenych (nepokrytych) policek
//   remainingPosSum – soucet kladnych hodnot vsech volnych policek
//                     (pouzivano pro rychly vypocet horniho odhadu)
//
// Jak funguje backtracking:
//   Pred kazdou zmenou stavu (umisteni/preskoceni) si zapamatujeme stary stav.
//   Po navratu z rekurze stav obnovime. Tim prozkoumame vsechny moznosti
//   bez nutnosti kopirovat celou desku
void dfs(SolverState& state, int startPos, int currentScore,
         int remainingPosSum) {
    int cols = state.board->cols;
    int totalCells = state.board->rows * cols;

    state.nodesVisited++;

    // --- Krok 1: Najdi prvni volne policko ---
    // Skenujeme radek po radku, zleva doprava.
    // Preskocime policka, ktera uz maji rozhodnuto (pokryta nebo preskocena).
    while (startPos < totalCells) {
        int r = startPos / cols;
        int c = startPos % cols;
        if (state.cellState[r][c] == 0) break;   // nasli jsme volne policko
        startPos++;
    }

    // --- Krok 2: Zadne volne policko => koncovy stav ---
    // Vsechna policka jsou rozhodnuta. Pokud je aktualni skore lepsi
    // nez dosavadni nejlepsi, ulozime toto reseni.
    if (startPos >= totalCells) {
        if (currentScore > state.bestScore) {
            state.bestScore = currentScore;
            state.bestCellState = state.cellState;
            state.bestPlacedPieces = state.placedPieces;
        }
        return;
    }

    int r = startPos / cols;
    int c = startPos % cols;

    // --- Krok 3: Orezavani (branch & bound) ---
    // Horni odhad: currentScore + soucet kladnych hodnot zbylych volnych policek.
    //
    // Logika: v nejlepsim moznem pripade bychom nechali vsechna kladna volna
    // policka nepokryta (jejich hodnoty se prictou ke skore) a pokryli bychom
    // vsechna zaporna (jejich hodnoty by se nepocitaly).
    //
    // Pokud ani tento optimisticky odhad neprekonava nejlepsi nalezene reseni,
    // nema smysl tuto vetev dale prozkoumavat => orezneme (pruning).
    if (currentScore + remainingPosSum <= state.bestScore) {
        return;
    }

    // --- Krok 4: Zkusime umistit quatromino pokryvajici policko (r, c) ---
    // Prochazime vsechna umisteni, kde (r,c) je prvni bunkou v poradi skenovani.
    // Pro kazde zkontrolujeme, zda jsou vsechny 4 bunky volne.
    const auto& candidates = state.placementsForCell[r][c];

    for (int plIdx : candidates) {
        const Placement& pl = (*state.placements)[plIdx];

        // Kontrola: jsou vsechny 4 bunky umisteni volne?
        bool canPlace = true;
        for (const auto& cell : pl.cells) {
            if (state.cellState[cell.row][cell.col] != 0) {
                canPlace = false;
                break;
            }
        }
        if (!canPlace) continue;

        // --- Umisteni quatromina ---
        // Zvysime pocitadlo kusu a oznacime 4 bunky timto ID.
        state.pieceCounter++;
        int coveredPosSum = 0;   // o kolik se snizi remainingPosSum
        for (const auto& cell : pl.cells) {
            state.cellState[cell.row][cell.col] = state.pieceCounter;
            int val = state.board->cells[cell.row][cell.col];
            if (val > 0) coveredPosSum += val;
        }
        state.placedPieces.push_back({pl.type, pl.cells});

        // Rekurze – currentScore se NEMENI, protoze pokryte bunky
        // nepridavaji nic ke skore (jsou "zakryte").
        dfs(state, startPos + 1, currentScore,
            remainingPosSum - coveredPosSum);

        // --- Backtracking ---
        // Vratime stav pred umistenim tohoto quatromina.
        state.placedPieces.pop_back();
        for (const auto& cell : pl.cells) {
            state.cellState[cell.row][cell.col] = 0;
        }
        state.pieceCounter--;
    }

    // --- Krok 5: Preskocime policko (nechat nepokryte) ---
    // Policko (r,c) zustane nepokryte. Jeho hodnota se pricte ke skore
    // (nebude zakryte quatrominem). Oznacime ho jako preskocene (-1),
    // aby ho DFS priste preskocilo pri hledani dalsiho volneho policka.
    state.cellState[r][c] = -1;
    int cellVal = state.board->cells[r][c];
    // Pokud hodnota policka byla kladna, odecteme ji z remainingPosSum
    // (uz neni "volne", je "preskocene")
    int posDelta = (cellVal > 0) ? cellVal : 0;

    dfs(state, startPos + 1, currentScore + cellVal,
        remainingPosSum - posDelta);

    state.cellState[r][c] = 0;   // backtracking – policko je zase volne
}

// Spusti DFS resic a vrati nejlepsi nalezene skore.
// Toto je hlavni vstupni bod resice.
int solve(SolverState& state) {
    // Soucet kladnych hodnot vsech policek = pocatecni remainingPosSum.
    // Pouziva se jako horni odhad v prvni iteraci.
    int remainingPosSum = 0;
    for (int r = 0; r < state.board->rows; r++) {
        for (int c = 0; c < state.board->cols; c++) {
            if (state.board->cells[r][c] > 0) {
                remainingPosSum += state.board->cells[r][c];
            }
        }
    }

    std::cout << "Spousteni DFS resice...\n";
    std::cout << "  Pocatecni horni odhad (soucet kladnych): "
              << remainingPosSum << "\n";

    // Spustime DFS od pozice 0 (levy horni roh = policko [0][0])
    dfs(state, 0, 0, remainingPosSum);

    std::cout << "DFS dokonceno.\n";
    std::cout << "  Prozkoumanych uzlu: " << state.nodesVisited << "\n";
    std::cout << "  Nejlepsi skore: " << state.bestScore << "\n";
    std::cout << "  Pocet umistenych quatromin: "
              << state.bestPlacedPieces.size() << "\n";

    return state.bestScore;
}

// ---------------------------------------------------------------------------
// Ukladani a zobrazeni vysledku
// ---------------------------------------------------------------------------

// Zapise reseni (maticovy popis pokryti) do vystupniho proudu.
//
// Format vystupu (priklad pro 4x4 desku):
//    T1  T1  T1   7     <- policka pokryta T1 a nepokryte policko s hodnotou 7
//     1  T1  L1   9     <- nepokryte (1, 9), pokryte T1 a L1
//     2   5  L1   4
//     3   4  L1  L1
//
// Kazde policko se zobrazuje jako:
//   - [T|L]<cislo> pokud je pokryte quatrominem (napr. T1, L2, T3, ...)
//   - ciselna hodnota policka pokud je nepokryte
//
// Cislovani quatromin probiha ZVLAST pro kazdy typ:
//   T-kusy: T1, T2, T3, ...
//   L-kusy: L1, L2, L3, ...
void writeSolution(std::ostream& out,
                   const Board& board,
                   const std::vector<std::vector<int>>& cellState,
                   const std::vector<PlacedPiece>& placedPieces,
                   int score) {

    // --- Prirazeni labelu kazdemu quatrominu ---
    // pieceLabels[i] = "T1", "L2", atd. pro i-ty umisteny kus (indexovano od 0).
    // V cellState jsou kusy cislovany od 1, takze kus s ID=k odpovida
    //   pieceLabels[k-1].
    int tCount = 0, lCount = 0;
    std::vector<std::string> pieceLabels(placedPieces.size());
    for (int i = 0; i < (int)placedPieces.size(); i++) {
        if (placedPieces[i].type == 'T') {
            tCount++;
            pieceLabels[i] = "T" + std::to_string(tCount);
        } else {
            lCount++;
            pieceLabels[i] = "L" + std::to_string(lCount);
        }
    }

    // --- Zjistime sirku pole pro zarovnani ---
    // Najdeme nejdelsi retezec (label nebo cislo) aby se sloupce zarovnaly.
    int fieldWidth = 1;
    for (const auto& label : pieceLabels) {
        fieldWidth = std::max(fieldWidth, (int)label.size());
    }
    for (int r = 0; r < board.rows; r++) {
        for (int c = 0; c < board.cols; c++) {
            fieldWidth = std::max(fieldWidth,
                (int)std::to_string(board.cells[r][c]).size());
        }
    }
    fieldWidth += 1;  // pridame 1 mezeru pro oddeleni sloupcu

    // --- Zapis matice ---
    for (int r = 0; r < board.rows; r++) {
        for (int c = 0; c < board.cols; c++) {
            std::string cellStr;
            int stateVal = cellState[r][c];

            if (stateVal > 0) {
                // Policko pokryte quatrominem – zobrazime jeho label
                cellStr = pieceLabels[stateVal - 1];
            } else {
                // Policko nepokryte – zobrazime jeho ciselnou hodnotu
                cellStr = std::to_string(board.cells[r][c]);
            }

            // Zarovnani doprava na fieldWidth znaku
            // (analogie Pythonu: cellStr.rjust(fieldWidth))
            int padding = fieldWidth - (int)cellStr.size();
            for (int p = 0; p < padding; p++) out << ' ';
            out << cellStr;
        }
        out << "\n";
    }

    // Doplnkove informace pod matici
    out << "\nSkore (soucet nepokrytych): " << score << "\n";
    out << "Pocet umistenych quatromin: " << placedPieces.size() << "\n";
    out << "  T: " << tCount << "\n";
    out << "  L: " << lCount << "\n";
}

// Ulozi reseni do souboru a zaroven ho vypise na konzoli.
void saveSolution(const Board& board,
                  const std::vector<std::vector<int>>& cellState,
                  const std::vector<PlacedPiece>& placedPieces,
                  int score,
                  const std::string& outputPath) {

    // Vytvorime slozku mapsol/ pokud neexistuje (vyzaduje C++17)
    std::filesystem::create_directories("mapsol");

    // Zapis do souboru
    std::ofstream fout(outputPath);
    if (!fout.is_open()) {
        std::cerr << "Nelze otevrit vystupni soubor: " << outputPath << "\n";
        return;
    }

    writeSolution(fout, board, cellState, placedPieces, score);
    fout.close();

    std::cout << "Reseni ulozeno do: " << outputPath << "\n";
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main(int argc, char* argv[]) {
    // --- Zpracovani parametru prikazove radky ---
    if (argc < 2) {
        std::cerr << "Pouziti: " << argv[0] << " <vstupni_soubor>\n";
        std::cerr << "  Priklad: " << argv[0] << " mapb6_6c.txt\n";
        return 1;
    }

    // Sestaveni cesty ke vstupnimu souboru (slozka mapb/)
    std::string inputName = argv[1];
    std::string inputPath = "mapb/" + inputName;

    // --- Nacteni desky ---
    Board board;
    try {
        board = loadBoard(inputPath);
    } catch (const std::exception& e) {
        std::cerr << "Chyba: " << e.what() << "\n";
        return 1;
    }

    std::cout << "Nactena deska ze souboru: " << inputPath << "\n";
    printBoard(board);

    // Vypocet souctu vsech policek (horni mez reseni bez pokryti)
    int totalSum = 0;
    for (int r = 0; r < board.rows; ++r) {
        for (int c = 0; c < board.cols; ++c) {
            totalSum += board.cells[r][c];
        }
    }
    std::cout << "Soucet vsech policek: " << totalSum << "\n\n";

    // --- Orientace quatromin T a L (zapsane rucne) ---
    // T ma 4 unikatni orientace, L ma 8 (vcetne zrcadlenych = tvar J).
    std::vector<Shape> orientationsT = makeShapesT();
    std::vector<Shape> orientationsL = makeShapesL();


    // Spojime vsechny orientace do jednoho vektoru
    std::vector<Shape> allOrientations;
    allOrientations.insert(allOrientations.end(),
                           orientationsT.begin(), orientationsT.end());
    allOrientations.insert(allOrientations.end(),
                           orientationsL.begin(), orientationsL.end());

    // --- Generovani vsech moznych umisteni na desce ---
    std::vector<Placement> allPlacements =
        generateAllPlacements(board, allOrientations);

    std::cout << "Celkovy pocet moznych umisteni: " 
              << allPlacements.size() << "\n";

    // Rozdeleni statistiky podle typu
    int countT = 0, countL = 0;
    for (const auto& pl : allPlacements) {
        if (pl.type == 'T') countT++;
        else countL++;
    }
    std::cout << "  - umisteni T: " << countT << "\n";
    std::cout << "  - umisteni L: " << countL << "\n\n";

    // --- Krok 3: DFS reseni s orezavanim (branch & bound) ---

    // Inicializace resice – pripravi stav desky a precompute
    // seznam umisteni pro kazde policko.
    SolverState solverState;
    initSolver(solverState, board, allPlacements);

    // Spusteni DFS. Funkce solve() prozkouma vsechny mozne konfigurace
    // a najde tu s maximalnim souctem nepokrytych policek.
    int bestScore = solve(solverState);

    // --- Vypis vysledku na konzoli ---
    std::cout << "\n========== VYSLEDEK ==========\n";
    std::cout << "Maximalni skore (soucet nepokrytych): " << bestScore << "\n";
    std::cout << "Soucet vsech policek: " << totalSum << "\n";
    std::cout << "Zlepseni pokrytim: " << (bestScore - totalSum) << "\n\n";

    // Zobrazime maticovy popis reseni na konzoli
    writeSolution(std::cout, board, solverState.bestCellState,
                  solverState.bestPlacedPieces, bestScore);

    // --- Krok 4: Ulozeni vysledku do souboru ---

    // Sestavime nazev vystupniho souboru s casovym razitkem (timestamp).
    // Priklad: mapb6_6c.txt  ->  mapsol/mapb6_6c_sol_20260225_143000.txt
    std::time_t now = std::time(nullptr);
    struct tm* lt = std::localtime(&now);
    char timeStr[64];
    std::strftime(timeStr, sizeof(timeStr), "%Y%m%d_%H%M%S", lt);

    // Odstranime priponu .txt z nazvu vstupniho souboru
    std::string baseName = inputName;
    size_t dotPos = baseName.rfind('.');
    if (dotPos != std::string::npos) {
        baseName = baseName.substr(0, dotPos);
    }

    std::string outputPath = "mapsol/" + baseName + "_sol_" + timeStr + ".txt";

    saveSolution(board, solverState.bestCellState,
                 solverState.bestPlacedPieces, bestScore, outputPath);

    return 0;
}
