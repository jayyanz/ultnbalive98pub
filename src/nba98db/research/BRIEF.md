# Research brief: NBA players for an NBA Live 98 roster mod

Goal: for each art key below, identify the real NBA player and collect bio and stats, as of the end of the 1996-97 NBA season. Data goes into a game database, so numbers must be exact.

## Identifying players
Each key = first 2 letters of the first name + first 5 letters of the surname, upper case, padded with "_" to 7 characters (Aaron McKie = AAMCKIE, Bob Sura = BOSURA_, "Lou Roe" = LOROE__). The players were NBA players around 1995-97 (the game's art was made in 1997). My best guess is given; confirm it, or if it is wrong find the player who fits the key and played in the NBA around 1996-97. If you cannot identify one with confidence, say so (identity_confidence "low") rather than guess.

## Source
basketball-reference.com player pages. The fetch tool only accepts URLs that came from a search result, so: WebSearch "<player name> basketball-reference" (standard mode), then WebFetch the www.basketball-reference.com/players/... URL from the results. Ask WebFetch for verbatim values; tell it not to round or alter numbers. If a fetch fails, try the aws.basketball-reference.com variant from search results, then other sources (ESPN, NBA.com) and record the source used.

## What to collect (one JSON object per player)
{
 "key": "DIRADJA", "first": "Dino", "last": "Radja", "identity_confidence": "high|medium|low",
 "source_url": "...",
 "jersey_1996_97": 40,            // last NBA team's number in 1996-97 (or last NBA season played); null if unknown
 "positions": "C-PF",             // as listed by the source
 "height_in": 83, "weight_lb": 225,
 "birth_date": "1967-04-24", "birth_city": "Split", "birth_state_or_country": "Croatia",
 "college": "None" ,              // last college as listed, or "None"
 "draft": {"year": 1989, "round": 2, "pick_in_round": 13, "overall": 40, "team": "Boston Celtics"},  // or null if undrafted
 "left_handed": null,             // true only if a source states it; else null
 "last_team_1996_97": "BOS",
 "seasons": [                      // every NBA regular season up to and including 1996-97, oldest first
   {"season": "1993-94", "team": "BOS", "teams_if_traded": ["BOS"], "G": 80, "GS": 78, "MP": 2303,
    "FG": 491, "FGA": 942, "3P": 0, "3PA": 0, "FT": 226, "FTA": 301, "ORB": 191, "DRB": 386,
    "AST": 114, "STL": 70, "BLK": 67, "TOV": 149, "PF": 276, "PTS": 1208}
 ],
 "notes": "anything uncertain"
}
- Use season TOTALS (not per game). For traded seasons give the combined TOT row and list the teams in order in teams_if_traded (team field "TOT").
- Use 3-letter team codes as the source shows them.
- Missing stats in old seasons (GS, 3P, ORB/DRB, STL, BLK, TOV before they were recorded): null, do not invent.
- Check every season: PTS must equal 2*FG + 3P + FT, and FG <= FGA, FT <= FTA. Re-fetch if a row fails.
- If the player did not play in the NBA in 1996-97, include all seasons up to his last one before 1997-98 and note it.

## Output
Write a JSON array to the output file named in your task (UTF-8). Then reply with a short summary: which players are confirmed, any low-confidence identities, any rows that failed the points check. Do not edit any other file.
