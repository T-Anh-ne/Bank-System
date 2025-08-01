#include "splashkit.h"
#include <vector>
#include <string>
#include <map>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <chrono> // For time-series analysis
#include <ctime>  // For time-series analysis

using namespace std;

// Forward declarations for UI functions
void draw_button(const string& text, float x, float y, float width, float height, color btn_clr, color txt_clr);
bool is_button_clicked(float x, float y, float width, float height);
string get_text_input(const string& prompt, float x, float y, float width, float height);

// --- Structs ---
struct Transaction {
    string date;
    string category;
    string description;
    float amount;
    char type; // 'I' (income) or 'E' (expense)
    int id;    // Unique ID for easy editing/deleting
};

struct UserProfile {
    string username;
    string password; // Added for security
    vector<Transaction> transactions;
    map<string, float> budgetPerCategory;
    int next_transaction_id = 1; // To ensure unique transaction IDs
};

// --- Global Variables (for UI context) ---
UserProfile* g_current_user = nullptr; // Pointer to the currently logged-in user
vector<UserProfile> g_users;           // All loaded users

// --- Utility Functions ---

/**
 * Draw text centered horizontally on the screen at specified vertical position
 */
void draw_text_centered(const string &text, int y, color clr = COLOR_BLACK) {
    if (!has_font("default_font"))
        load_font("default_font", "arial.ttf");
    font fnt = font_named("default_font");
    int w = text_width(text, "default_font", 20);
    int x = (screen_width() - w) / 2;
    draw_text(text, clr, "default_font", 20, x, y);
}

/**
 * Format float amount as a string with two decimal places
 */
string format_amount(float amount) {
    ostringstream oss;
    oss << fixed << setprecision(2) << amount;
    return oss.str();
}

/**
 * Draw a menu item at given coordinates with specified color
 */
void draw_menu_item(const string &text, int x, int y, color clr = COLOR_BLACK) {
    draw_text(text, clr, x, y);
}

/**
 * Calculate total expense amount grouped by category from transaction list
 */
map<string, float> calculateExpensesByCategory(const vector<Transaction>& transactions) {
    map<string, float> expenses;
    for (const auto& t : transactions) {
        if (t.type == 'E') expenses[t.category] += t.amount;
    }
    return expenses;
}

/**
 * Wait for a mouse left button click to continue
 * Shows a prompt and then blocks until user clicks anywhere on screen
 */
void wait_for_mouse_click_to_return() {
    draw_text_centered("Click anywhere to return", screen_height() - 50);
    refresh_screen();

    while (!quit_requested()) {
        process_events();
        if (mouse_clicked(LEFT_BUTTON)) return;
        delay(10); // Prevent busy-waiting
    }
}

/**
 * Parse YYYY-MM-DD date string into year, month, day integers
 */
bool parse_date(const string& date_str, int& year, int& month, int& day) {
    stringstream ss(date_str);
    char dash;
    if (ss >> year >> dash && dash == '-' && ss >> month >> dash && dash == '-' && ss >> day) {
        return true;
    }
    return false;
}

// --- UI Interaction Functions ---

/**
 * Draw a clickable button
 */
void draw_button(const string& text, float x, float y, float width, float height, color btn_clr, color txt_clr) {
    fill_rectangle(btn_clr, x, y, width, height);
    draw_rectangle(COLOR_BLACK, x, y, width, height); // Border
    draw_text(text, txt_clr, x + (width - text_width(text, "default_font", 16)) / 2, y + (height - text_height("Tg", "default_font", 16)) / 2);
}

/**
 * Check if a button is clicked
 */
bool is_button_clicked(float x, float y, float width, float height) {
    return mouse_clicked(LEFT_BUTTON) && point_in_rectangle(mouse_position(), rectangle_from(x, y, width, height));
}

/**
 * Get text input from user via SplashKit window.
 * Returns empty string if user cancels (e.g., presses ESC).
 * This function clears the screen for input and draws its own prompt.
 */
string get_text_input(const string& prompt, float x_input, float y_input, float width, float height) {
    string input = "";
    bool done = false;

    // Clear screen once at the beginning of this input session
    clear_screen(COLOR_WHITE);

    // Draw static prompt and instructions
    draw_text_centered(prompt, y_input - 50);
    draw_text("Press ENTER to confirm, ESC to cancel", COLOR_GRAY, x_input, y_input + height + 10);

    while (!quit_requested() && !done) {
        process_events();

        // Clear the input box area before redrawing to prevent residue
        // This ensures the blinking cursor and input text are drawn cleanly each frame
        fill_rectangle(COLOR_WHITE, x_input - 5, y_input - 5, width + 10, height + 10);
        draw_rectangle(COLOR_BLACK, x_input - 5, y_input - 5, width + 10, height + 10); // Input box border

        // Draw input text with blinking cursor
        if (static_cast<int>(current_ticks() / 500) % 2 == 0) {
            draw_text(input + "|", COLOR_BLACK, x_input, y_input);
        } else {
            draw_text(input, COLOR_BLACK, x_input, y_input);
        }
        
        refresh_screen();

        if (key_typed(RETURN_KEY)) {
            done = true;
        } else if (key_typed(ESCAPE_KEY)) {
            return ""; // Cancelled
        } else if (key_typed(BACKSPACE_KEY)) {
            if (!input.empty()) {
                input.pop_back();
            }
        } else {
            // Check all possible key codes for printable characters
            for (int k = 32; k <= 126; ++k) { // ASCII printable range
                if (key_typed(static_cast<key_code>(k))) {
                    char key_char = static_cast<char>(k);
                    input += key_char;
                }
            }
        }
        delay(10);
    }
    return input;
}


// --- Transaction Management ---

/**
 * Add a new transaction via UI input
 */
void add_transaction_ui(UserProfile& user) {
    clear_screen(COLOR_WHITE);
    draw_text_centered("--- Add New Transaction ---", 50);

    string date = get_text_input("Enter Date (YYYY-MM-DD):", 200, 100, 400, 30);
    if (date.empty()) return;

    string category = get_text_input("Enter Category:", 200, 150, 400, 30);
    if (category.empty()) return;

    string description = get_text_input("Enter Description:", 200, 200, 400, 30);
    if (description.empty()) return;

    string amount_str = get_text_input("Enter Amount (number):", 200, 250, 400, 30);
    if (amount_str.empty()) return;

    float amount;
    try {
        amount = stof(amount_str);
    } catch (...) {
        clear_screen(COLOR_WHITE); // Clear before showing error
        draw_text_centered("Invalid amount. Please enter a valid number.", screen_height() / 2);
        wait_for_mouse_click_to_return();
        return;
    }

    string type_str = get_text_input("Enter Type (I for Income, E for Expense):", 200, 300, 400, 30);
    if (type_str.empty() || (toupper(type_str[0]) != 'I' && toupper(type_str[0]) != 'E')) {
        clear_screen(COLOR_WHITE); // Clear before showing error
        draw_text_centered("Invalid type. Must be 'I' or 'E'.", screen_height() / 2);
        wait_for_mouse_click_to_return();
        return;
    }
    char type = toupper(type_str[0]);

    user.transactions.push_back({date, category, description, amount, type, user.next_transaction_id++});
    clear_screen(COLOR_WHITE); // Clear before showing success
    draw_text_centered("Transaction added successfully!", screen_height() / 2);
    wait_for_mouse_click_to_return();
}

/**
 * Display all transactions on screen.
 * If categoryFilter is provided (non-empty), only show transactions of that category.
 */
void draw_transactions(const vector<Transaction>& transactions, const string& categoryFilter = "") {
    clear_screen(COLOR_WHITE);
    draw_text_centered("--- Transactions ---", 20);

    int y = 60;
    // Display header
    draw_text("ID | Date       | Category  | Description                | Type   | Amount", COLOR_BLACK, 20, y);
    y += 25;
    draw_line(COLOR_BLACK, 15, y, screen_width() - 15, y);
    y += 10;

    for (const auto& t : transactions) {
        if (!categoryFilter.empty() && t.category != categoryFilter) continue;

        string line = to_string(t.id) + " | " + t.date + " | " + t.category + " | " + t.description.substr(0, 25) + (t.description.length() > 25 ? "..." : "") + " | " + (t.type == 'I' ? "Income" : "Expense") + " | $" + format_amount(t.amount);
        draw_text(line, COLOR_BLACK, 20, y);
        y += 25;
        if (y > screen_height() - 80) { // Leave space for "Click to return"
            draw_text_centered("... (More transactions below) ...", y);
            break;
        }
    }

    wait_for_mouse_click_to_return();
}

/**
 * Edit or Delete a transaction
 */
void edit_delete_transaction_ui(UserProfile& user) {
    clear_screen(COLOR_WHITE);
    draw_text_centered("--- Edit/Delete Transaction ---", 50);

    // Show transactions to help user choose
    draw_transactions(user.transactions); 

    // Re-draw the header for edit/delete screen after view
    clear_screen(COLOR_WHITE);
    draw_text_centered("--- Edit/Delete Transaction ---", 50);

    string id_str = get_text_input("Enter ID of transaction to edit/delete:", 200, 100, 400, 30);
    if (id_str.empty()) return;

    int id_to_find;
    try {
        id_to_find = stoi(id_str);
    } catch (...) {
        clear_screen(COLOR_WHITE);
        draw_text_centered("Invalid ID.", screen_height() / 2);
        wait_for_mouse_click_to_return();
        return;
    }

    auto it = find_if(user.transactions.begin(), user.transactions.end(), [&](const Transaction& t) {
        return t.id == id_to_find;
    });

    if (it == user.transactions.end()) {
        clear_screen(COLOR_WHITE);
        draw_text_centered("Transaction with ID " + id_str + " not found.", screen_height() / 2);
        wait_for_mouse_click_to_return();
        return;
    }

    // Found transaction, now give options
    Transaction& t = *it;
    clear_screen(COLOR_WHITE);
    draw_text_centered("Transaction found:", 50);
    draw_text("ID: " + to_string(t.id), COLOR_BLACK, 50, 100);
    draw_text("Date: " + t.date, COLOR_BLACK, 50, 120);
    draw_text("Category: " + t.category, COLOR_BLACK, 50, 140);
    draw_text("Description: " + t.description, COLOR_BLACK, 50, 160);
    draw_text("Amount: $" + format_amount(t.amount), COLOR_BLACK, 50, 180);
    draw_text("Type: " + std::string(t.type == 'I' ? "Income" : "Expense"), COLOR_BLACK, 50, 200);

    float btn_width = 100;
    float btn_height = 40;
    float btn_spacing = 20;
    float start_x = (screen_width() - (btn_width * 3 + btn_spacing * 2)) / 2; // Center the group of buttons

    draw_button("Edit", start_x, 250, btn_width, btn_height, COLOR_LIGHT_GRAY, COLOR_BLACK);
    draw_button("Delete", start_x + btn_width + btn_spacing, 250, btn_width, btn_height, COLOR_LIGHT_GRAY, COLOR_BLACK);
    draw_button("Cancel", start_x + 2 * (btn_width + btn_spacing), 250, btn_width, btn_height, COLOR_LIGHT_GRAY, COLOR_BLACK);
    refresh_screen();

    while (!quit_requested()) {
        process_events();
        if (is_button_clicked(start_x, 250, btn_width, btn_height)) { // Edit button
            string new_date = get_text_input("New Date (YYYY-MM-DD) [" + t.date + "]:", 200, 100, 400, 30);
            if (!new_date.empty()) t.date = new_date;

            string new_category = get_text_input("New Category [" + t.category + "]:", 200, 150, 400, 30);
            if (!new_category.empty()) t.category = new_category;

            string new_description = get_text_input("New Description [" + t.description + "]:", 200, 200, 400, 30);
            if (!new_description.empty()) t.description = new_description;

            string new_amount_str = get_text_input("New Amount (number) [" + format_amount(t.amount) + "]:", 200, 250, 400, 30);
            if (!new_amount_str.empty()) {
                try {
                    t.amount = stof(new_amount_str);
                } catch (...) {
                    clear_screen(COLOR_WHITE);
                    draw_text_centered("Invalid amount. Not updated.", screen_height() / 2);
                    wait_for_mouse_click_to_return();
                }
            }

            string new_type_str = get_text_input("New Type (I/E) [" + string(1, t.type) + "]:", 200, 300, 400, 30);
            if (!new_type_str.empty() && (toupper(new_type_str[0]) == 'I' || toupper(new_type_str[0]) == 'E')) {
                t.type = toupper(new_type_str[0]);
            } else if (!new_type_str.empty()) {
                 clear_screen(COLOR_WHITE);
                 draw_text_centered("Invalid type. Not updated.", screen_height() / 2);
                 wait_for_mouse_click_to_return();
            }

            clear_screen(COLOR_WHITE);
            draw_text_centered("Transaction updated!", screen_height() / 2);
            wait_for_mouse_click_to_return();
            return;
        }
        else if (is_button_clicked(start_x + btn_width + btn_spacing, 250, btn_width, btn_height)) { // Delete button
            user.transactions.erase(it);
            clear_screen(COLOR_WHITE);
            draw_text_centered("Transaction deleted!", screen_height() / 2);
            wait_for_mouse_click_to_return();
            return;
        }
        else if (is_button_clicked(start_x + 2 * (btn_width + btn_spacing), 250, btn_width, btn_height)) { // Cancel button
            return;
        }
        delay(10);
    }
}


/**
 * Calculate and display summary: total income, total expense, net amount
 */
void draw_summary(const vector<Transaction>& transactions) {
    clear_screen(COLOR_WHITE);
    draw_text_centered("--- Financial Summary ---", 20);

    float totalIncome = 0, totalExpense = 0;
    for (const auto& t : transactions) {
        if (t.type == 'I') totalIncome += t.amount;
        else if (t.type == 'E') totalExpense += t.amount;
    }

    draw_text("Total Income: $" + format_amount(totalIncome), COLOR_GREEN, 50, 80);
    draw_text("Total Expense: $" + format_amount(totalExpense), COLOR_RED, 50, 120);
    draw_text("Net: $" + format_amount(totalIncome - totalExpense), COLOR_BLUE, 50, 160);

    wait_for_mouse_click_to_return();
}

/**
 * Display budget report comparing budgeted amount vs spent amount per category.
 * Shows categories in red if spending exceeds budget and gives warnings.
 */
void drawBudgetReport(const UserProfile& user) {
    clear_screen(COLOR_WHITE);
    draw_text_centered("--- Budget Report for " + user.username + " ---", 20);

    auto expenses = calculateExpensesByCategory(user.transactions);

    int y = 80;
    bool budget_exceeded_any_category = false;
    for (const auto& [cat, budget] : user.budgetPerCategory) {
        float spent = expenses[cat];
        string line = cat + ": Budget = $" + format_amount(budget) + ", Spent = $" + format_amount(spent);
        color display_color = COLOR_BLACK;
        if (spent > budget) {
            display_color = COLOR_RED;
            budget_exceeded_any_category = true;
        } else if (budget > 0 && spent / budget >= 0.9) { // Warn if close to budget (90% or more)
             display_color = COLOR_ORANGE;
        }
        draw_text(line, display_color, 50, y);
        y += 30;
    }

    if (budget_exceeded_any_category) {
        draw_text_centered("WARNING: You have exceeded budget in one or more categories!", screen_height() - 80, COLOR_RED);
    } else if (user.budgetPerCategory.empty()){
        draw_text_centered("No budget categories set. Go to 'Set Budget' to add some!", screen_height() / 2, COLOR_GRAY);
    }


    wait_for_mouse_click_to_return();
}

/**
 * UI to set/edit budget per category
 */
void set_budget_ui(UserProfile& user) {
    clear_screen(COLOR_WHITE);
    draw_text_centered("--- Set Budget Per Category ---", 50);

    // Display current budgets
    int y_current_budgets = 100;
    draw_text("Current Budgets:", COLOR_BLACK, 50, y_current_budgets);
    y_current_budgets += 20;
    if (user.budgetPerCategory.empty()) {
        draw_text("No budgets set yet.", COLOR_GRAY, 70, y_current_budgets);
        y_current_budgets += 20;
    } else {
        for (const auto& [cat, budget] : user.budgetPerCategory) {
            draw_text(cat + ": $" + format_amount(budget), COLOR_BLACK, 70, y_current_budgets);
            y_current_budgets += 20;
        }
    }


    string category = get_text_input("Enter Category to set budget for (e.g., Food, Transport):", 200, y_current_budgets + 50, 400, 30);
    if (category.empty()) return;

    string amount_str = get_text_input("Enter Budget Amount for " + category + ":", 200, y_current_budgets + 100, 400, 30);
    if (amount_str.empty()) return;

    float amount;
    try {
        amount = stof(amount_str);
    } catch (...) {
        clear_screen(COLOR_WHITE);
        draw_text_centered("Invalid amount. Please enter a valid number.", screen_height() / 2);
        wait_for_mouse_click_to_return();
        return;
    }

    user.budgetPerCategory[category] = amount;
    clear_screen(COLOR_WHITE);
    draw_text_centered("Budget for " + category + " set to $" + format_amount(amount) + "!", screen_height() / 2);
    wait_for_mouse_click_to_return();
}

/**
 * Display time-series report (monthly/yearly summary)
 */
void draw_time_series_report(const UserProfile& user) {
    clear_screen(COLOR_WHITE);
    draw_text_centered("--- Time Series Report ---", 20);

    map<string, float> monthly_income;
    map<string, float> monthly_expense;
    map<string, float> yearly_income;
    map<string, float> yearly_expense;

    for (const auto& t : user.transactions) {
        int year, month, day;
        if (parse_date(t.date, year, month, day)) {
            // Format month_key as YYYY-MM to ensure correct sorting
            string month_key = to_string(year) + "-" + (month < 10 ? "0" : "") + to_string(month);
            string year_key = to_string(year);

            if (t.type == 'I') {
                monthly_income[month_key] += t.amount;
                yearly_income[year_key] += t.amount;
            } else {
                monthly_expense[month_key] += t.amount;
                yearly_expense[year_key] += t.amount;
            }
        }
    }

    int y = 60;
    draw_text("Monthly Summary:", COLOR_BLACK, 50, y);
    y += 25;
    // Sort monthly data by key
    vector<pair<string, float>> sorted_monthly_income(monthly_income.begin(), monthly_income.end());
    sort(sorted_monthly_income.begin(), sorted_monthly_income.end());

    for (const auto& pair : sorted_monthly_income) {
        string month_year = pair.first;
        float income = pair.second;
        float expense = monthly_expense[month_year]; // Get corresponding expense
        draw_text(month_year + ": Income=$" + format_amount(income) + ", Expense=$" + format_amount(expense) + ", Net=$" + format_amount(income - expense), COLOR_BLACK, 70, y);
        y += 20;
    }

    y += 30; // Spacer
    draw_text("Yearly Summary:", COLOR_BLACK, 50, y);
    y += 25;
    // Sort yearly data by key
    vector<pair<string, float>> sorted_yearly_income(yearly_income.begin(), yearly_income.end());
    sort(sorted_yearly_income.begin(), sorted_yearly_income.end());

    for (const auto& pair : sorted_yearly_income) {
        string year = pair.first;
        float income = pair.second;
        float expense = yearly_expense[year]; // Get corresponding expense
        draw_text(year + ": Income=$" + format_amount(income) + ", Expense=$" + format_amount(expense) + ", Net=$" + format_amount(income - expense), COLOR_BLACK, 70, y);
        y += 20;
    }

    wait_for_mouse_click_to_return();
}


// --- File Management ---

/**
 * Save all user profiles with transactions and budgets to file "users.txt"
 */
void saveToFile(const vector<UserProfile>& users) {
    ofstream ofs("users.txt");
    if (!ofs.is_open()) {
        write_line("ERROR: Could not open users.txt for saving.");
        return;
    }
    for (const auto& user : users) {
        ofs << "USER|" << user.username << "|" << user.password << "\n"; // Save username and password
        ofs << "NEXT_ID|" << user.next_transaction_id << "\n"; // Save next transaction ID for continuity

        ofs << "BUDGETS|";
        for (const auto& [cat, val] : user.budgetPerCategory) {
            ofs << cat << ":" << val << ",";
        }
        ofs << "\n";

        for (const auto& t : user.transactions) {
            ofs << "TRANS|" << t.id << "|" << t.date << "|" << t.category << "|" << t.description << "|" << t.amount << "|" << t.type << "\n";
        }
        ofs << "ENDUSER\n";
    }
    ofs.close();
}

/**
 * Load all user profiles from file "users.txt" including their transactions and budgets
 */
void loadFromFile(vector<UserProfile>& users) {
    ifstream ifs("users.txt");
    if (!ifs.is_open()) return;

    users.clear();

    string line;
    UserProfile* currentUser = nullptr;
    while (getline(ifs, line)) {
        if (line.rfind("USER|", 0) == 0) {
            users.push_back(UserProfile());
            currentUser = &users.back();
            stringstream ss(line);
            string token;
            getline(ss, token, '|'); // Read "USER" token
            getline(ss, currentUser->username, '|'); // Read username
            getline(ss, currentUser->password, '|'); // Read password
        } else if (line.rfind("NEXT_ID|", 0) == 0 && currentUser != nullptr) {
            currentUser->next_transaction_id = stoi(line.substr(8));
        }
        else if (line.rfind("BUDGETS|", 0) == 0 && currentUser != nullptr) {
            string budgets_str = line.substr(8);
            stringstream ss(budgets_str);
            string part;
            while (getline(ss, part, ',')) {
                if (part.empty()) continue;
                auto pos = part.find(':');
                if (pos != string::npos) {
                    string cat = part.substr(0, pos);
                    float val = stof(part.substr(pos + 1));
                    currentUser->budgetPerCategory[cat] = val;
                }
            }
        } else if (line.rfind("TRANS|", 0) == 0 && currentUser != nullptr) {
            stringstream ss(line);
            string token;
            vector<string> parts;
            // Use a loop to split by '|'
            while (getline(ss, token, '|')) {
                parts.push_back(token);
            }
            if (parts.size() == 7) { // Expect 7 parts: "TRANS", id, date, category, desc, amount, type
                int id = stoi(parts[1]);
                string date = parts[2];
                string cat = parts[3];
                string desc = parts[4];
                float amount = stof(parts[5]);
                char type = parts[6][0];
                currentUser->transactions.push_back({date, cat, desc, amount, type, id});
            }
        } else if (line == "ENDUSER") {
            currentUser = nullptr;
        }
    }
    ifs.close();
}

// --- Authentication and User Management ---

/**
 * Handles user login/registration. Returns true if successful login/registration, false if user exits.
 */
bool handle_user_authentication() {
    clear_screen(COLOR_WHITE);
    draw_text_centered("Welcome to Personal Finance Tracker", 50);

    float btn_width = 150;
    float btn_height = 50;
    float btn_x = (screen_width() - btn_width) / 2; // Center horizontally
    float btn_y_start = 150;
    float btn_spacing = 60; // Vertical spacing between buttons

    draw_button("Login", btn_x, btn_y_start, btn_width, btn_height, COLOR_LIGHT_GRAY, COLOR_BLACK);
    draw_button("Register", btn_x, btn_y_start + btn_spacing, btn_width, btn_height, COLOR_LIGHT_GRAY, COLOR_BLACK);
    draw_button("Exit App", btn_x, btn_y_start + 2 * btn_spacing, btn_width, btn_height, COLOR_LIGHT_GRAY, COLOR_BLACK);
    refresh_screen();

    string username_input, password_input;

    while (!quit_requested()) {
        process_events();

        if (is_button_clicked(btn_x, btn_y_start, btn_width, btn_height)) { // Login button
            username_input = get_text_input("Enter Username:", 200, 300, 400, 30);
            if (username_input.empty()) return false; // User cancelled
            password_input = get_text_input("Enter Password:", 200, 350, 400, 30);
            if (password_input.empty()) return false; // User cancelled

            auto it = find_if(g_users.begin(), g_users.end(), [&](const UserProfile& u) {
                return u.username == username_input;
            });

            if (it != g_users.end() && it->password == password_input) {
                g_current_user = &(*it);
                clear_screen(COLOR_WHITE); // Clear before success message
                draw_text_centered("Login successful!", screen_height() / 2);
                wait_for_mouse_click_to_return();
                return true; // Login successful
            } else {
                clear_screen(COLOR_WHITE); // Clear before error message
                draw_text_centered("Invalid username or password.", screen_height() / 2);
                wait_for_mouse_click_to_return(); // Pause to let user read
                return false; // Login failed, return to auth screen
            }
        }
        else if (is_button_clicked(btn_x, btn_y_start + btn_spacing, btn_width, btn_height)) { // Register button
            username_input = get_text_input("Choose Username:", 200, 300, 400, 30);
            if (username_input.empty()) return false;
            password_input = get_text_input("Choose Password:", 200, 350, 400, 30);
            if (password_input.empty()) return false;

            // Check if username already exists
            if (find_if(g_users.begin(), g_users.end(), [&](const UserProfile& u) { return u.username == username_input; }) != g_users.end()) {
                clear_screen(COLOR_WHITE); // Clear before error message
                draw_text_centered("Username already taken. Please choose another.", screen_height() / 2);
                wait_for_mouse_click_to_return();
                return false;
            }

            g_users.push_back(UserProfile{username_input, password_input});
            g_current_user = &g_users.back();
            saveToFile(g_users); // Save new user
            clear_screen(COLOR_WHITE); // Clear before success message
            draw_text_centered("Registration successful! Logged in as " + username_input, screen_height() / 2);
            wait_for_mouse_click_to_return();
            return true; // Registration successful
        }
        else if (is_button_clicked(btn_x, btn_y_start + 2 * btn_spacing, btn_width, btn_height)) { // Exit button
            return false; // Indicate exit from app
        }
        delay(10);
    }
    return false; // Quit requested
}


// --- Main Program ---
int main() {
    open_window("Personal Finance Tracker", 800, 600);
    load_font("default_font", "arial.ttf"); // Ensure font is loaded early

    loadFromFile(g_users); // Load all users at startup

    while (!quit_requested()) {
        if (g_current_user == nullptr) { // Not logged in
            if (!handle_user_authentication()) {
                break; // Exit app if authentication fails or user chooses to exit
            }
        } else { // Logged in
            clear_screen(COLOR_WHITE);
            draw_text_centered("Welcome, " + g_current_user->username + "!", 20);

            // Menu Buttons
            float btn_x = 50;
            float btn_y_start = 80;
            float btn_width = 250;
            float btn_height = 40;
            float btn_spacing = 50; // Vertical spacing between buttons

            draw_button("1. Add Transaction", btn_x, btn_y_start, btn_width, btn_height, COLOR_LIGHT_GRAY, COLOR_BLACK);
            draw_button("2. View All Transactions", btn_x, btn_y_start + btn_spacing, btn_width, btn_height, COLOR_LIGHT_GRAY, COLOR_BLACK);
            draw_button("3. Edit/Delete Transaction", btn_x, btn_y_start + 2 * btn_spacing, btn_width, btn_height, COLOR_LIGHT_GRAY, COLOR_BLACK);
            draw_button("4. Show Summary", btn_x, btn_y_start + 3 * btn_spacing, btn_width, btn_height, COLOR_LIGHT_GRAY, COLOR_BLACK);
            draw_button("5. Budget Report", btn_x, btn_y_start + 4 * btn_spacing, btn_width, btn_height, COLOR_LIGHT_GRAY, COLOR_BLACK);
            draw_button("6. Set Budget", btn_x, btn_y_start + 5 * btn_spacing, btn_width, btn_height, COLOR_LIGHT_GRAY, COLOR_BLACK);
            draw_button("7. Time Series Report", btn_x, btn_y_start + 6 * btn_spacing, btn_width, btn_height, COLOR_LIGHT_GRAY, COLOR_BLACK);
            draw_button("8. Logout", btn_x, btn_y_start + 7 * btn_spacing, btn_width, btn_height, COLOR_LIGHT_GRAY, COLOR_BLACK);
            draw_button("9. Exit App", btn_x, btn_y_start + 8 * btn_spacing, btn_width, btn_height, COLOR_LIGHT_GRAY, COLOR_BLACK);

            refresh_screen();
            process_events();

            if (is_button_clicked(btn_x, btn_y_start, btn_width, btn_height)) { // Add Transaction
                add_transaction_ui(*g_current_user);
                saveToFile(g_users);
            }
            else if (is_button_clicked(btn_x, btn_y_start + btn_spacing, btn_width, btn_height)) { // View All
                draw_transactions(g_current_user->transactions);
            }
            else if (is_button_clicked(btn_x, btn_y_start + 2 * btn_spacing, btn_width, btn_height)) { // Edit/Delete
                edit_delete_transaction_ui(*g_current_user);
                saveToFile(g_users);
            }
            else if (is_button_clicked(btn_x, btn_y_start + 3 * btn_spacing, btn_width, btn_height)) { // Show Summary
                draw_summary(g_current_user->transactions);
            }
            else if (is_button_clicked(btn_x, btn_y_start + 4 * btn_spacing, btn_width, btn_height)) { // Budget Report
                drawBudgetReport(*g_current_user);
            }
            else if (is_button_clicked(btn_x, btn_y_start + 5 * btn_spacing, btn_width, btn_height)) { // Set Budget
                set_budget_ui(*g_current_user);
                saveToFile(g_users);
            }
            else if (is_button_clicked(btn_x, btn_y_start + 6 * btn_spacing, btn_width, btn_height)) { // Time Series Report
                draw_time_series_report(*g_current_user);
            }
            else if (is_button_clicked(btn_x, btn_y_start + 7 * btn_spacing, btn_width, btn_height)) { // Logout
                g_current_user = nullptr; // Set current user to null to go back to login screen
            }
            else if (is_button_clicked(btn_x, btn_y_start + 8 * btn_spacing, btn_width, btn_height)) { // Exit App
                break;
            }
        }
        delay(10); // Reduce CPU usage
    }

    saveToFile(g_users); // Save all data before exiting
    close_window("Personal Finance Tracker");
    return 0;
}
