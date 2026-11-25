"""Where the game files are. Defaults assume the project layout
<project>/Original ISO Extract, <project>/Original Files and <project>/src/nba98db/tools.
Override with NBA98_ORIGDB (folder holding the CD's PLAYERS.DBF) and NBA98_ART
(the installed game's art folder)."""
import os
_PROJ = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..', '..'))
ORIGDB = os.environ.get('NBA98_ORIGDB', os.path.join(_PROJ, 'Original ISO Extract', 'NBA98', 'ORIGDBE')) + os.sep
ART = os.environ.get('NBA98_ART', os.path.join(_PROJ, 'Original Files', 'art'))
