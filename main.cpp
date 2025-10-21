#include <bits/stdc++.h>
using namespace std;

struct ProblemState {
    // before freeze: count wrong attempts; if solved, wrong_before_first and solve_time
    int wrong_before = 0;
    bool solved = false;
    int solve_time = 0; // time of first AC
    // frozen tracking
    bool frozen = false; // this problem is currently in frozen set for team
    int submissions_after_freeze = 0; // y in -x/y or 0/y
    int wrong_before_freeze = 0; // x in -x/y or 0/y (equals wrong_before at freeze moment)

    // dynamic counts during scroll
    int frozen_wrong = 0; // wrong submissions during frozen period before first AC reveal
    bool has_frozen_ac = false; // whether there exists an AC among frozen submissions
    int frozen_ac_time = 0; // time of the first AC within frozen period
};

struct Team {
    string name;
    // per problem 0..M-1
    vector<ProblemState> probs;

    // live scoreboard metrics excluding frozen outcomes
    int solved_count = 0;
    long long penalty = 0; // 20 * wrong_before_first + solve_time sum over solved (excluding frozen)
    // for tie-breaker: multiset of solve times of solved problems (descending when comparing)
    vector<int> solve_times; // store times for solved (non-frozen) problems

    // overall submissions for QUERY_SUBMISSION (store last by filters)
    struct Submission { char prob; string status; int time; };
    vector<Submission> submissions; // all submissions (including after freeze)

    // helper to update sorting key
    vector<int> solve_times_sorted_desc() const {
        vector<int> s = solve_times;
        sort(s.begin(), s.end(), greater<int>());
        return s;
    }
};

struct ScoreboardState {
    // after last FLUSH or derived at moment of SCROLL (flush first)
    vector<string> order; // team names in ranked order
    unordered_map<string,int> rank_index; // name -> 0-based index
};

struct SystemState {
    bool started = false;
    int duration = 0;
    int M = 0; // problems A..A+M-1

    // teams by name
    map<string, Team> teams; // ordered by name for initial ranking before first flush

    // global freeze state
    bool frozen = false;

    // snapshot for FLUSH/QUERY ranking (excluding frozen outcomes)
    ScoreboardState last_board;
};

static inline int problemIndex(char c) { return c - 'A'; }

static bool is_ac(const string &s){ return s == "Accepted"; }
static bool is_wrong(const string &s){ return s=="Wrong_Answer" || s=="Runtime_Error" || s=="Time_Limit_Exceed"; }

static int cmpTeams(const Team &a, const Team &b){
    if (a.solved_count != b.solved_count) return a.solved_count > b.solved_count ? -1 : 1;
    if (a.penalty != b.penalty) return a.penalty < b.penalty ? -1 : 1;
    // compare solve time sequences: smaller lexicographically when sorted descending (max first, then next)
    vector<int> sa = a.solve_times_sorted_desc();
    vector<int> sb = b.solve_times_sorted_desc();
    size_t n = max(sa.size(), sb.size());
    sa.resize(n, 0);
    sb.resize(n, 0);
    for (size_t i=0;i<n;i++){
        if (sa[i] != sb[i]) return sa[i] < sb[i] ? -1 : 1; // smaller max time wins
    }
    if (a.name != b.name) return a.name < b.name ? -1 : 1;
    return 0;
}

static void recompute_board(SystemState &S, ScoreboardState &board){
    // build vector of refs
    vector<reference_wrapper<const Team>> vec;
    vec.reserve(S.teams.size());
    for (auto &kv : S.teams) vec.push_back(cref(kv.second));
    sort(vec.begin(), vec.end(), [](const Team& x, const Team& y){ return cmpTeams(x,y) < 0; });
    board.order.clear();
    board.rank_index.clear();
    for (size_t i=0;i<vec.size();++i){
        const string &nm = vec[i].get().name;
        board.order.push_back(nm);
        board.rank_index[nm] = (int)i;
    }
}

static void apply_solve(Team &t, ProblemState &ps, int time){
    ps.solved = true;
    ps.solve_time = time;
    t.solved_count += 1;
    t.penalty += 20LL * ps.wrong_before + time;
    t.solve_times.push_back(time);
}

static void unapply_solve(Team &t, ProblemState &ps){
    // used during scroll re-ranking steps when we need temporary previous? Not needed.
}

int main(){
    ios::sync_with_stdio(false);
    cin.tie(nullptr);

    SystemState S;

    string cmd;
    while (cin >> cmd){
        if (cmd == "ADDTEAM"){
            string name; cin >> name;
            if (S.started){
                cout << "[Error]Add failed: competition has started.\n";
                continue;
            }
            if (S.teams.count(name)){
                cout << "[Error]Add failed: duplicated team name.\n";
                continue;
            }
            Team t; t.name = name;
            S.teams[name] = std::move(t);
            cout << "[Info]Add successfully.\n";
        } else if (cmd == "START"){
            string a,b; // DURATION [t] PROBLEM [m]
            cin >> a >> S.duration >> b >> S.M;
            if (S.started){
                cout << "[Error]Start failed: competition has started.\n";
                continue;
            }
            S.started = true;
            for (auto &kv : S.teams){
                kv.second.probs.assign(S.M, ProblemState());
            }
            cout << "[Info]Competition starts.\n";
        } else if (cmd == "SUBMIT"){
            char prob; string by, team_name, with, status, at; int time;
            cin >> prob >> by >> team_name >> with >> status >> at >> time; // format guaranteed
            Team &t = S.teams[team_name];
            // record submission for queries
            t.submissions.push_back({prob, status, time});
            int idx = problemIndex(prob);
            if (idx < 0 || idx >= S.M) continue; // safe
            ProblemState &ps = t.probs[idx];

            if (S.frozen && !ps.solved){
                // During freeze, for unsolved problems, we do not update live metrics; we only accumulate frozen info.
                if (!ps.frozen){
                    ps.frozen = true;
                    ps.wrong_before_freeze = ps.wrong_before;
                    ps.submissions_after_freeze = 0;
                    ps.frozen_wrong = 0;
                    ps.has_frozen_ac = false;
                    ps.frozen_ac_time = 0;
                }
                ps.submissions_after_freeze += 1;
                if (is_ac(status)){
                    if (!ps.has_frozen_ac){
                        ps.has_frozen_ac = true;
                        ps.frozen_ac_time = time;
                    }
                } else if (is_wrong(status)){
                    if (!ps.has_frozen_ac) ps.frozen_wrong += 1;
                }
            } else {
                // Not in freeze effect for this problem
                if (ps.solved){
                    // already solved earlier; further submissions do nothing
                } else if (is_ac(status)){
                    apply_solve(t, ps, time);
                } else if (is_wrong(status)){
                    ps.wwrong_before += 1;
                }
            }
        } else if (cmd == "FLUSH"){
            cout << "[Info]Flush scoreboard.\n";
            // recompute last_board ranking snapshot excluding frozen outcomes
            recompute_board(S, S.last_board);
        } else if (cmd == "FREEZE"){
            if (S.frozen){
                cout << "[Error]Freeze failed: scoreboard has been frozen.\n";
                continue;
            }
            S.frozen = true;
            // initialize frozen tracking snapshots but do not mark as frozen until a post-freeze submission occurs
            for (auto &kv : S.teams){
                Team &t = kv.second;
                for (int i=0;i<S.M;i++){
                    ProblemState &ps = t.probs[i];
                    ps.wrong_before_freeze = ps.wrong_before;
                    ps.submissions_after_freeze = 0;
                    ps.frozen_wrong = 0;
                    ps.has_frozen_ac = false;
                    ps.frozen_ac_time = 0;
                    // Do not force ps.frozen here; only becomes frozen upon first post-freeze submission if unsolved
                }
            }
            cout << "[Info]Freeze scoreboard.\n";
        } else if (cmd == "SCROLL"){
            if (!S.frozen){
                cout << "[Error]Scroll failed: scoreboard has not been frozen.\n";
                continue;
            }
            cout << "[Info]Scroll scoreboard.\n";
            // As per spec, scroll first flushes the scoreboard before proceeding
            recompute_board(S, S.last_board);
            // print scoreboard before scrolling (after flush)
            // prepare helper lambda to print one team line
            auto print_team_line = [&](const Team &t, int ranking){
                cout << t.name << ' ' << ranking << ' ' << t.solved_count << ' ' << t.penalty;
                for (int i=0;i<S.M;i++){
                    const ProblemState &ps = t.probs[i];
                    if (!ps.frozen){
                        if (ps.solved){
                            if (ps.wrong_before == 0) cout << " +"; else cout << ' ' << "+" << ps.wrong_before;
                        } else {
                            if (ps.wrong_before == 0) cout << " ."; else cout << ' ' << "-" << ps.wrong_before;
                        }
                    } else {
                        // frozen
                        if (ps.wrong_before_freeze == 0) cout << ' ' << "0/" << ps.submissions_after_freeze;
                        else cout << ' ' << "-" << ps.wrong_before_freeze << '/' << ps.submissions_after_freeze;
                    }
                }
                cout << '\n';
            };
            // print using current last_board order
            for (size_t i=0;i<S.last_board.order.size();++i){
                const string &nm = S.last_board.order[i];
                const Team &t = S.teams[nm];
                print_team_line(t, (int)i+1);
            }

            // Create a working order for scrolling that updates as we reveal
            vector<string> order = S.last_board.order;
            auto recompute_order = [&](){
                // sort current teams by current metrics
                vector<reference_wrapper<const Team>> vec;
                vec.reserve(S.teams.size());
                for (auto &kv : S.teams) vec.push_back(cref(kv.second));
                sort(vec.begin(), vec.end(), [](const Team& x, const Team& y){ return cmpTeams(x,y) < 0; });
                order.clear();
                for (auto &r : vec) order.push_back(r.get().name);
            };

            // Prepare quick index lookup
            auto rebuild_index = [&](unordered_map<string,int> &idx){
                idx.clear();
                for (int i=0;i<(int)order.size();++i) idx[order[i]] = i;
            };
            unordered_map<string,int> ord_index; rebuild_index(ord_index);

            // Determine if any team has frozen problems
            auto team_has_frozen = [&](const Team &t){
                for (int i=0;i<S.M;i++) if (t.probs[i].frozen && !t.probs[i].solved) return true;
                return false;
            };

            // Scroll loop
            while (true){
                // pick lowest-ranked team with frozen problems
                int pick = -1;
                for (int i=(int)order.size()-1; i>=0; --i){
                    Team &t = S.teams[order[i]];
                    if (team_has_frozen(t)) { pick = i; break; }
                }
                if (pick == -1) break; // no frozen problems remaining
                Team &team = S.teams[order[pick]];
                // find smallest problem id among frozen problems of this team
                int probIdx = -1;
                for (int i=0;i<S.M;i++){
                    ProblemState &ps = team.probs[i];
                    if (ps.frozen && !ps.solved){ probIdx = i; break; }
                }
                if (probIdx == -1) { // shouldn't happen, but safe
                    // clear frozen flags if any inconsistency
                    for (int i=0;i<S.M;i++) team.probs[i].frozen = false;
                    break;
                }
                ProblemState &ps = team.probs[probIdx];

                // Unfreeze this problem: incorporate frozen outcomes
                bool rank_changed = false;
                if (ps.has_frozen_ac){
                    // this problem becomes solved with wrong_before + frozen_wrong, time=frozen_ac_time
                    // Update team metrics
                    int added_wrong = ps.frozen_wrong;
                    int ac_time = ps.frozen_ac_time;
                    // update wrong_before to include wrongs before AC (pre + during)
                    ps.wrong_before += added_wrong;
                    apply_solve(team, ps, ac_time);
                    rank_changed = true; // solving may change ranks
                } else {
                    // only wrong submissions during freeze: accumulate to wrong_before
                    if (ps.submissions_after_freeze > 0){
                        ps.wrong_before += ps.frozen_wrong;
                        // no rank change from unsolved wrong attempts (penalty unaffected until solved)
                    }
                }
                // clear frozen markers for this problem
                ps.frozen = false;
                ps.submissions_after_freeze = 0;

                // If rank potentially changed, recompute order and emit swaps as per problem:
                // "output each unfreeze that causes a ranking change during scrolling, one per line"
                // When a team's ranking increases, we need to print: team_name1 team_name2 solved_number penalty_time
                // where team_name2 is the team at the position before team_name1's increase (the immediate one it overtakes),
                // and we print for each change step.
                if (rank_changed){
                    // Rebuild order by resorting from scratch, then compare previous index vs new index
                    vector<string> old_order = order;
                    recompute_order();
                    // find positions of this team in old/new orders
                    int old_pos = -1, new_pos = -1;
                    for (int i=0;i<(int)old_order.size();++i) if (old_order[i]==team.name) old_pos = i;
                    for (int i=0;i<(int)order.size();++i) if (order[i]==team.name) new_pos = i;
                    if (old_pos != -1 && new_pos != -1 && new_pos < old_pos){
                        // Output exactly one line per unfreeze that changes ranking.
                        // team_name2 is the team previously at the new_pos (i.e., the position team moves into).
                        const string &replaced = old_order[new_pos];
                        cout << team.name << ' ' << replaced << ' ' << team.solved_count << ' ' << team.penalty << "\n";
                    }
                    rebuild_index(ord_index);
                }

                // continue loop
            }

            // After scrolling ends, output final scoreboard and lift frozen state entirely
            // First, recompute order and then print
            recompute_order();
            for (size_t i=0;i<order.size();++i){
                const Team &t = S.teams[order[i]];
                // ensure no problem remains marked frozen for output (they should be cleared per unfreeze)
                // but just in case none remaining
                cout << t.name << ' ' << (int)i+1 << ' ' << t.solved_count << ' ' << t.penalty;
                for (int j=0;j<S.M;j++){
                    const ProblemState &ps2 = t.probs[j];
                    if (ps2.solved){
                        if (ps2.wrong_before == 0) cout << " +"; else cout << ' ' << "+" << ps2.wrong_before;
                    } else {
                        if (ps2.wrong_before == 0) cout << " ."; else cout << ' ' << "-" << ps2.wrong_before;
                    }
                }
                cout << '\n';
            }
            // lift frozen state globally
            S.frozen = false;
            // clear any lingering frozen flags
            for (auto &kv : S.teams){
                for (int i=0;i<S.M;i++){
                    kv.second.probs[i].frozen = false;
                    kv.second.probs[i].submissions_after_freeze = 0;
                }
            }
        } else if (cmd == "QUERY_RANKING"){
            string team_name; cin >> team_name;
            if (!S.teams.count(team_name)){
                cout << "[Error]Query ranking failed: cannot find the team.\n";
                continue;
            }
            cout << "[Info]Complete query ranking.\n";
            if (S.frozen) cout << "[Warning]Scoreboard is frozen. The ranking may be inaccurate until it were scrolled.\n";
            // ranking is based on last_board snapshot
            if (S.last_board.order.empty()){
                // before first FLUSH, rank by lexicographic order of team names
                // build by lexicographic names
                vector<string> names;
                names.reserve(S.teams.size());
                for (auto &kv : S.teams) names.push_back(kv.first);
                sort(names.begin(), names.end());
                int rank = (int)(lower_bound(names.begin(), names.end(), team_name) - names.begin()) + 1;
                cout << team_name << " NOW AT RANKING " << rank << "\n";
            } else {
                auto it = S.last_board.rank_index.find(team_name);
                int rank = 0;
                if (it != S.last_board.rank_index.end()) rank = it->second + 1; else rank = (int)S.last_board.order.size();
                cout << team_name << " NOW AT RANKING " << rank << "\n";
            }
        } else if (cmd == "QUERY_SUBMISSION"){
            string team_name, where, probtok, andtok, statustok;
            cin >> team_name >> where >> probtok >> andtok >> statustok; // parse tokens
            if (!S.teams.count(team_name)){
                cout << "[Error]Query submission failed: cannot find the team.\n";
                continue;
            }
            auto parse_field = [&](const string &tok, const string &prefix){
                size_t p = tok.find('=');
                if (p==string::npos) return string();
                return tok.substr(p+1);
            };
            string probv = parse_field(probtok, "PROBLEM=");
            string statusv = parse_field(statustok, "STATUS=");
            Team &t = S.teams[team_name];
            bool found = false;
            Team::Submission last;
            for (int i=(int)t.submissions.size()-1; i>=0; --i){
                const auto &s = t.submissions[i];
                bool prob_ok = (probv == "ALL" || s.prob == probv[0]);
                bool status_ok = (statusv == "ALL" || s.status == statusv);
                if (prob_ok && status_ok){ last = s; found = true; break; }
            }
            cout << "[Info]Complete query submission.\n";
            if (!found){
                cout << "Cannot find any submission.\n";
            } else {
                cout << t.name << ' ' << last.prob << ' ' << last.status << ' ' << last.time << "\n";
            }
        } else if (cmd == "END"){
            cout << "[Info]Competition ends.\n";
            break;
        } else {
            // ignore unknown
        }
    }
    return 0;
}
