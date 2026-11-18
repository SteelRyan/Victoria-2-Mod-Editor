// Made by tools/gen_blockcode_catalogue.py from legacy/modeditor/blockcode.py — don't edit by hand.
static const char *const CATALOGUE_CONDITIONS =
    R"CATALOGUE(
# The nation
cp tag | the nation is {} (on a province: its owner) | tag
* exists | this nation exists: {} (or pick a nation: that one exists) | tagbool
c ai | the nation is played by the computer: {} | bool
cp is_greater_power | the nation is a great power: {} | bool
c is_secondary_power | the nation is a secondary power: {} | bool
cp civilized | the nation is civilised: {} | bool
c civilization_progress | an uncivilised nation is at least {} of the way to being civilised (0–1) | num
c rank | the nation's rank is {} or better (1 is the top) | int
c prestige | the nation's prestige is at least {} | num
c badboy | the nation's infamy is at least {} (a share of the limit: 0.5 = half of it) | num
c money | the nation has at least {} money | num
c treasury | the nation has at least {} money in its treasury | num
c government | the nation's government is {} | gov
c nationalvalue | the nation's national value is {} | nv
cp primary_culture | the nation's primary culture is {} | culture
cp primary_culture | the nation's primary culture is that of {} | tag
c accepted_culture | the nation accepts the culture {} | culture
cs is_culture_group | the culture (a nation's primary one, a pop's own) belongs to the group {} | cgroup
cs is_culture_group | the culture belongs to the same group as the primary culture of {} | tag
c capital | the nation's capital is province {} | prov
c num_of_cities | the nation owns at least {} provinces | int
c number_of_states | the nation has at least {} states | int
c num_of_ports | the nation has at least {} ports | int
c total_num_of_ports | the nation holds at least {} ports (its own and the ones it occupies) | int
c total_amount_of_divisions | the nation has at least {} brigades | int
c total_amount_of_ships | the nation has at least {} ships | int
c military_score | the nation's military score is at least {} | int
c military_score | the nation's military score is higher than that of {} | tag
c industrial_score | the nation's industrial score is at least {} | int
c plurality | the nation's plurality is at least {} | num
c revanchism | the nation's revanchism is at least {} (0–1) | num
c war_exhaustion | the nation's war exhaustion is at least {} | num
c colonial_nation | the nation has colonies: {} | bool
c is_mobilised | the nation is mobilised: {} | bool
c is_disarmed | the nation is disarmed: {} | bool
c has_recently_lost_war | the nation lost a war not long ago: {} | bool
c lost_national | at least {} of the nation's core land is lost (0–1) | num
c national_provinces_occupied | at least {} of the nation's core land is occupied (0–1) | num
c num_of_revolts | rebels hold at least {} of the nation's provinces | int
c revolt_percentage | at least {} of the nation's provinces are in revolt (0–1) | num
c owns | the nation owns province {} | prov
c controls | the nation controls province {} | prov
c is_canal_enabled | canal number {} is open | int
c culture_has_union_tag | the nation's culture group has a union nation: {} | bool
c has_unclaimed_cores | the nation has cores it doesn't own: {} | bool
c has_leader | the nation has a general or admiral named {} | text
cp produces | {} is produced here | goods
c in_default | the nation is bankrupt: {} (yes / no — or a nation: that one is) | tagbool
c blockade | at least {} of the nation's ports are blockaded (0–1) | num
c <goods> | the nation has at least {} of {key} in its stockpile | num
c is_core | province {} is a core of the nation | prov
# Politics
c ruling_party_ideology | the nation's ruling party is {} | ideology
c ruling_party | the nation's ruling party is the one named {} | text
c upper_house | in the nation's upper house, {ideology} holds at least {value} (a share: 0–1) | {ideology:ideology value:num}
* is_ideology_enabled | the ideology {} has appeared in the world | ideology
c is_next_reform | the nation's next possible reform is {} | option
cp <issue> | the nation's {key} is {} | option
cp <tech> | the nation has researched {key}: {} (1 = yes, 0 = no) | int
c election | an election is going on in the nation: {} | bool
c constructing_cb_type | the nation is justifying the war goal {} | cb
c constructing_cb_progress | the nation's justifying of a war goal is at least {} done | num
cs political_reform_want | the people's wish for political reform is at least {} (0–1) | num
cs social_reform_want | the people's wish for social reform is at least {} (0–1) | num
c poor_tax | the nation taxes the poor at least {} (0–1) | num
c middle_tax | the nation taxes the middle class at least {} (0–1) | num
c rich_tax | the nation taxes the rich at least {} (0–1) | num
c education_spending | the nation spends at least {} on education (0–1) | num
c administration_spending | the nation spends at least {} on administration (0–1) | num
c military_spending | the nation spends at least {} on the military (0–1) | num
c social_spending | the nation spends at least {} on social care (0–1) | num
c can_nationalize | the nation can nationalise foreign factories: {} | bool
c invention | the nation has the invention {} | invention
c tech_school | the nation's technology school is {} | text
c crime_fighting | the nation spends at least {} on crime fighting (0–1) | num
# War & diplomacy
c war | the nation is at war: {} | bool
c war_with | the nation is at war with {} | tag
c alliance_with | the nation is allied with {} | tag
c truce_with | the nation has a truce with {} | tag
c neighbour | the nation borders {} | tag
c relation | the nation's relations with {who} are at least {value} (-200 to 200) | {who:tag value:num}
c casus_belli | the nation has a reason for war against {} | tag
c is_vassal | the nation is someone's puppet: {} | bool
c vassal_of | the nation is a puppet of {} | tag
c is_our_vassal | {} is the nation's puppet | tag
c is_substate | the nation is a substate: {} | bool
c substate_of | the nation is a substate of {} | tag
c is_independant | the nation is independent: {} | bool
c in_sphere | the nation is in the sphere of {} | tag
c is_sphere_leader_of | the nation has {} in its sphere | tag
c part_of_sphere | the nation is in some great power's sphere: {} | bool
c num_of_allies | the nation has at least {} allies | int
c num_of_vassals | the nation has at least {} puppets | int
c num_of_vassals_no_substates | the nation has at least {} puppets that aren't substates | int
cp is_cultural_union | the nation is the union nation of its culture group: {} | tagbool
c this_culture_union | the nation's culture group has the same union nation as {} | tag
c stronger_army_than | the nation's army is stronger than that of {} | tag
c brigades_compare | the nation has at least {} times the brigades of the other nation (THIS / FROM) | num
c is_releasable_vassal | the nation can be released as a puppet: {} | tagbool
c is_possible_vassal | the nation could release {} as its puppet | tag
c can_create_vassals | the nation could release some nation as its puppet: {} | bool
)CATALOGUE"
    R"CATALOGUE(c has_cultural_sphere | a nation of its own culture group is in its sphere: {} | bool
c diplomatic_influence | the nation's influence in {who} is at least {value} | {who:tag value:num}
c war_score | the nation's war score in a war is at least {} | num
c total_sunk_by_us | the nation has sunk at least {} ships | int
c military_access | the nation has military access through {} | tag
c involved_in_crisis | the nation takes part in the crisis: {} | bool
c is_liberation_crisis | the crisis going on is about freeing a nation: {} | bool
c is_claim_crisis | the crisis going on is about a claimed state: {} | bool
# Flags, modifiers, numbers
cp has_country_flag | the nation has the flag {} (a marker an effect set earlier) | cflag
cp has_country_modifier | the nation has the modifier {} | mod
* has_global_flag | the world flag {} is set (a marker for the whole game) | gflag
c check_variable | the nation's number “{which}” is at least {value} | {which:var value:num}
p has_province_flag | the province has the flag {} | pflag
p has_province_modifier | the province has the modifier {} | mod
# Time
* year | the year is {} or later | int
* month | the month is {} or later (0 = January … 11 = December) | int
* great_wars_enabled | great wars have begun: {} | bool
* world_wars_enabled | world wars have begun: {} | bool
* crisis_exist | a crisis is going on somewhere: {} | bool
* always | always: {} (yes: always true — no: never; to switch a part off) | bool
# The province
p province_id | the province is number {} | prov
p owned_by | the province belongs to {} | tag
p controlled_by | the province is held by {} | tag
p is_core | the province is a core of {} | tag
p province_control_days | someone other than its owner has held the province for at least {} days | int
p country_units_in_province | troops of {} stand in the province | tag
p state_id | the province lies in the same state as province {} | prov
p is_capital | the province is its nation's capital: {} | bool
p is_state_capital | the province is its state's capital: {} | bool
p is_coastal | the province lies on the sea: {} | bool
p is_colonial | the province is a colony: {} | bool
p is_overseas | the province is overseas: {} | bool
p is_blockaded | the province is blockaded: {} | bool
p port | the province has a port: {} | bool
p has_building | the province has a {} | building
p can_build_in_province | a {building} can be built in the province (not above the world's best level: {limit_to_world_greatest_level}) | {building:building limit_to_world_greatest_level?:bool}
p has_factories | the province's state has factories: {} | bool
p trade_goods | the province produces {} | goods
p terrain | the province's terrain is {} | terrain
cp continent | it lies on the continent {} (a nation: its capital) | continent
p region | the province lies in the state {} | region
p life_rating | the province's life rating is at least {} | int
p controlled_by_rebels | rebels hold the province: {} | bool
p units_in_province | at least {} brigades stand in the province | int
p has_empty_adjacent_province | an unowned province lies next to the province: {} | bool
p has_national_minority | people of a culture that isn't accepted live in the province: {} | bool
p has_crime | the province has the crime {} | crime
p flashpoint_tension | the tension in the province is at least {} | num
p has_flashpoint | the province's state is a flashpoint: {} | bool
p has_recent_imigration | people have moved into the province in the last {} days | int
cp unit_in_battle | a battle is being fought here: {} | bool
p empty | the province belongs to nobody: {} | bool
# The people
cps has_pop_culture | people of the culture {} live here | culture
cps has_pop_religion | people of the religion {} live here | religion
cps has_pop_type | pops of the type {} live here | poptype
p <poptype> | at least {} of the people in the province are {key} (a share: 0–1) | num
cps <ideology> | at least {} % of the people here follow the ideology {key} | num
cp <option> | at least {} % of the people here want {key} | num
cps poor_strata_militancy | the militancy of the poor is at least {} (0–10) | num
cps middle_strata_militancy | the militancy of the middle class is at least {} (0–10) | num
cps rich_strata_militancy | the militancy of the rich is at least {} (0–10) | num
cps poor_strata_life_needs | the poor get at least {} of their life needs (0–1) | num
cps middle_strata_life_needs | the middle class gets at least {} of its life needs (0–1) | num
cps rich_strata_life_needs | the rich get at least {} of their life needs (0–1) | num
cps poor_strata_everyday_needs | the poor get at least {} of their everyday needs (0–1) | num
cps middle_strata_everyday_needs | the middle class gets at least {} of its everyday needs (0–1) | num
cps rich_strata_everyday_needs | the rich get at least {} of their everyday needs (0–1) | num
cps poor_strata_luxury_needs | the poor get at least {} of their luxury needs (0–1) | num
cps middle_strata_luxury_needs | the middle class gets at least {} of its luxury needs (0–1) | num
cps rich_strata_luxury_needs | the rich get at least {} of their luxury needs (0–1) | num
cp pop_majority_culture | most people here are {} | culture
cp pop_majority_religion | most people here follow {} | religion
cps pop_majority_ideology | most people here follow the ideology {} | ideology
cp pop_majority_issue | most people here want {} | option
cps total_pops | at least {} people live here | int
cps average_militancy | the people's average militancy is at least {} (0–10) | num
cps average_consciousness | the people's average consciousness is at least {} (0–10) | num
cps literacy | the people's literacy is at least {} (0–1) | num
cps militancy | the people's militancy is at least {} (0–10) | num
cps consciousness | the people's consciousness is at least {} (0–10) | num
cps unemployment | at least {} of the people are out of work (0–1) | num
p unemployment_by_type | at least {value} of the {type} in the province are out of work (0–1) | {type:poptype value:num}
p work_available | there is work in the province for {worker} | {worker:poptype}
p pop_militancy | some pop in the province has a militancy of at least {} | num
cps is_primary_culture | the people are of the nation's primary culture: {} | bool
cps is_primary_culture | the people are of the primary culture of {} | tag
cps is_accepted_culture | the people are of an accepted culture: {} | bool
ps is_state_religion | the people have the state religion: {} | bool
cps culture | the people's culture is {} | culture
cps religion | the religion here is {} | religion
s type | the pop is of the type {} | poptype
s pop_type | the pop is of the type {} (the game's events write it both ways) | poptype
s strata | the pop is {} | strata
ps is_slave | the pop is enslaved: {} | bool
s life_needs | the pop gets at least {} of its life needs (0–1) | num
)CATALOGUE"
    R"CATALOGUE(s everyday_needs | the pop gets at least {} of its everyday needs (0–1) | num
s luxury_needs | the pop gets at least {} of its luxury needs (0–1) | num
s cash_reserves | the pop's savings are at least {} | num
s agree_with_ruling_party | the pop agrees with at least {} of the ruling party's views (0–1) | num
s political_movement | the pop is part of a political movement: {} | bool
s social_movement | the pop is part of a social movement: {} | bool
# All / any / none
* AND | ALL of these are true: | ->
* OR | at least ONE of these is true: | ->
* NOT | NONE of these is true: | ->
# About someone else
* <tag> | the nation {key}: | ->c
* <prov> | the province nr. {key}: | ->p
* <region> | the state {key}: | ->p
cp any_owned_province | at least one of the nation's (in a state: the state's) provinces: | ->p
c all_core | every one of the nation's core provinces: | ->p
c any_core | at least one of the nation's core provinces: | ->p
p any_core | at least one nation with a core on the province: | ->c
c capital_scope | the nation's capital province: | ->p
c any_state | at least one of the nation's states: | ->p
c any_neighbor_country | at least one neighbouring nation: | ->c
* any_greater_power | at least one great power: | ->c
c any_sphere_member | at least one nation of its sphere: | ->c
c any_substate | at least one of the nation's substates: | ->c
c war_countries | every nation it is at war with: | ->c
c sphere_owner | the great power whose sphere the nation is in: | ->c
c overlord | the nation's overlord: | ->c
c cultural_union | the union nation of its culture group: | ->c
cp any_pop | at least one pop here: | ->s
p owner | the province's owner (a nation): | ->c
p controller | the nation that holds the province: | ->c
p any_neighbor_province | at least one province next to it: | ->p
p state_scope | the province's whole state: | ->p
s location | the province the pop lives in: | ->p
s country | the pop's nation: | ->c
* THIS | the nation this event is about (THIS): | ->c
* FROM | the nation that sent it (FROM): | ->c
)CATALOGUE";
static const char *const CATALOGUE_EFFECTS =
    R"CATALOGUE(
# The nation
c prestige | give the nation {} prestige (a minus takes it away) | num
c badboy | give the nation {} infamy (a minus takes it away) | num
c treasury | give the nation {} money (a minus takes it away) | num
c plurality | change the nation's plurality by {} | num
c research_points | give the nation {} research points | num
c years_of_research | give the nation {} years' worth of research points | num
c leadership | give the nation {} leadership points | num
c war_exhaustion | change the nation's war exhaustion by {} | num
c government | change the nation's government to {} | gov
c nationalvalue | change the nation's national value to {} | nv
c civilized | make the nation civilised: {} | bool
c primary_culture | change the nation's primary culture to {} | culture
c primary_culture | the nation's primary culture becomes that of {} | tag
c add_accepted_culture | the nation now accepts the culture {} | culture
c remove_accepted_culture | the nation no longer accepts the culture {} | culture
c religion | change the nation's state religion to {} | religion
c capital | move the nation's capital to province {} | prov
c change_tag | the nation becomes {} (new name, flag and tag) | tagc
c change_tag_no_core_switch | the nation becomes {} (a player keeps the old nation) | tag
c add_tax_relative_income | give the nation {} of a year's tax income as money | num
c build_factory_in_capital_state | build a {} in the state of the nation's capital | building
c enable_canal | open canal number {} | int
c kill_leader | the general or admiral named {} dies | text
c define_general | a general named {name} joins the nation — personality {personality}, background {background} | {name:text personality:personality background:background picture?:text}
c define_admiral | an admiral named {name} joins the nation — personality {personality}, background {background} | {name:text personality:personality background:background picture?:text}
c nationalize | the nation takes over the factories foreigners built in it: {} | bool
c <goods> | add {} of {key} to the nation's stockpile (a minus takes away) | num
c build_railway_in_capital | build a railroad in the nation's capital (in its whole state: {in_whole_capital_state} — not above the world's best level: {limit_to_world_greatest_level}) | {in_whole_capital_state:bool limit_to_world_greatest_level:bool}
c build_fort_in_capital | build a fort in the nation's capital (in its whole state: {in_whole_capital_state} — not above the world's best level: {limit_to_world_greatest_level}) | {in_whole_capital_state:bool limit_to_world_greatest_level:bool}
# Politics
c ruling_party_ideology | put a party of the ideology {} in power | ideology
c political_reform | pass the political reform {} | option
c social_reform | pass the social reform {} | option
c economic_reform | pass the economic reform {} (uncivilised nations) | option
c military_reform | pass the military reform {} (uncivilised nations) | option
c remove_random_economic_reforms | the nation loses {} of its economic reforms, picked at random | int
c remove_random_military_reforms | the nation loses {} of its military reforms, picked at random | int
-c <issue> | set the nation's {key} to {} | option
c activate_technology | give the nation the technology {} | tech
* enable_ideology | the ideology {} appears in the world | ideology
c election | the nation calls an election: {} | bool
c upper_house | in the upper house, {ideology} gains {value} (a share: 0.1 = 10 %) | {ideology:ideology value:num}
cps scaled_militancy | followers of {ideology}: their militancy changes by {factor} (more, the stronger they hold it) | {ideology:ideology factor:num}
cps scaled_militancy | people who want {issue}: their militancy changes by {factor} (more, the more they want it) | {issue:option factor:num}
cps scaled_consciousness | followers of {ideology}: their consciousness changes by {factor} | {ideology:ideology factor:num}
cps scaled_consciousness | people who want {issue}: their consciousness changes by {factor} | {issue:option factor:num}
c prestige_factor | change the nation's prestige by {} of what it has (0.1 = +10 %) | num
c tech_school | the nation's technology school becomes {} | text
* add_crisis_temperature | the crisis heats up by {} | num
c add_crisis_interest | the nation takes an interest in the crisis going on: {} | bool
* great_wars_enabled | great wars begin: {} | bool
* world_wars_enabled | world wars begin: {} | bool
# War & diplomacy
c relation | change the nation's relations with {who} by {value} | {who:tag value:num}
c diplomatic_influence | change the nation's influence in {who} by {value} | {who:tag value:num}
c war | declare war on {target} (its allies are called in: {call_ally}) — the war goals: | ->goal target:tag call_ally?:bool
c casus_belli | the nation gains the war goal {type} against {target} for {months} months (about the state of province {state_province_id}) | {target:tag type:cb months?:int state_province_id?:prov}
c add_casus_belli | the nation gains the war goal {type} against {target} for {months} months, kept when used (about the state of province {state_province_id}) | {target:tag type:cb months?:int state_province_id?:prov}
c remove_casus_belli | the nation loses the war goal {type} against {target} | {target:tag type:cb}
c create_alliance | the nation allies with {} | tag
c leave_alliance | the nation breaks its alliance with {} | tag
c military_access | the nation gains military access through {} | tag
c end_military_access | the nation loses military access through {} | tag
c end_war | the nation makes peace with {} | tag
c inherit | the nation swallows {} whole | tag
c annex_to | the nation is swallowed by {} | tag
c release | the nation releases {} as an independent nation | tag
c release_vassal | the nation releases {} as its puppet | tag
c create_vassal | the nation makes {} its puppet | tag
c add_to_sphere | the nation takes {} into its sphere | tag
c remove_from_sphere | the nation drops {} from its sphere | tag
c neutrality | the nation becomes neutral (leaves its alliances): {} | bool
# Flags, modifiers, numbers
c set_country_flag | set the flag {} on the nation (a marker conditions can ask for) | cflag
c clr_country_flag | clear the nation's flag {} | cflag
* set_global_flag | set the world flag {} | gflag
* clr_global_flag | clear the world flag {} | gflag
c add_country_modifier | give the nation the modifier {name} for {duration} days (-1: for ever) | {name:mod duration:int}
c remove_country_modifier | take the modifier {} away from the nation | mod
c set_variable | set the nation's number “{which}” to {value} | {which:var value:num}
c change_variable | add {value} to the nation's number “{which}” | {which:var value:num}
p set_province_flag | set the flag {} on the province | pflag
p clr_province_flag | clear the province's flag {} | pflag
)CATALOGUE"
    R"CATALOGUE(p add_province_modifier | give the province the modifier {name} for {duration} days (-1: for ever) | {name:mod duration:int}
p remove_province_modifier | take the modifier {} away from the province | mod
# Fire another event
c country_event | fire the nation event {} | event
p province_event | fire the province event {} | event
# The province
p secede_province | hand the province to {} | tag
p change_controller | the province is now held by {} | tag
p is_slave | the province's state becomes a slave state: {} | bool
p add_crime | the province gets the crime {} | crime
p party_loyalty | loyalty to the {ideology} parties changes by {loyalty_value} in the province | {ideology:ideology loyalty_value:num}
cp add_core | add a core: {} (in a province: a nation's tag — for a nation: a province number) | text
cp remove_core | remove a core: {} (in a province: a nation's tag — for a nation: a province number) | text
p life_rating | change the province's life rating by {} | int
p trade_goods | the province now produces {} | goods
p infrastructure | change the province's railroad level by {} | int
p fort | change the province's fort level by {} | int
p naval_base | change the province's naval base level by {} | int
p change_province_name | rename the province to {} | text
p change_region_name | rename the province's state to {} | text
p flashpoint_tension | change the tension in the province by {} | num
p sub_unit | raise a unit of {type} — in province {value} (“current”: this one) | {type:unit value:text}
# The people
cps militancy | change the people's militancy by {} | num
cps consciousness | change the people's consciousness by {} | num
cps literacy | change the people's literacy by {} (0.01 = 1 %) | num
cs money | give {} money | num
ps reduce_pop | multiply the pop's size by {} (0.9: a tenth leaves — 1.1: a tenth more) | num
ps pop_type | the pop becomes {} | poptype
s move_pop | the pop moves to province {} | prov
ps assimilate | the pop takes on the nation's culture: {} | bool
cps ideology | {factor} of the people turn to the ideology {value} (0.1 = a tenth) | {factor:num value:ideology}
cps dominant_issue | {factor} of the people now want {value} | {factor:num value:option}
cps move_issue_percentage | {value} of the people who want {from} now want {to} (a share: 0–1) | {from:option to:option value:num}
# Chance
* random | with a chance of {chance} in 100, do this: | ->= chance:int
* random_list | do exactly ONE of these (the bigger its weight, the more likely): | ->list
# Only where … (a condition inside an effect)
* limit | but only where ALL of this is true: | ->cond
# For someone else
* <tag> | for the nation {key}: | ->c
* <prov> | for the province nr. {key}: | ->p
* <region> | for the state {key}: | ->p
cp <poptype> | for every pop of the type {key} here: | ->s
cp any_owned | for every one of its provinces: | ->p
cp poor_strata | for every poor pop: | ->s
cp middle_strata | for every pop of the middle class: | ->s
cp rich_strata | for every rich pop: | ->s
cp random_owned | for one of the nation's (in a state: the state's) provinces, picked at random: | ->p
c all_core | for every one of the nation's core provinces: | ->p
c capital_scope | for the nation's capital province: | ->p
c any_state | for every one of the nation's states: | ->p
c random_state | for one of the nation's states, picked at random: | ->p
cp any_pop | for every pop here: | ->s
cp random_pop | for one pop here, picked at random: | ->s
* any_country | for every nation of the world: | ->c
* random_country | for one nation, picked at random: | ->c
c any_neighbor_country | for every neighbouring nation: | ->c
c any_substate | for every one of the nation's substates: | ->c
c random_neighbor_country | for one neighbouring nation, picked at random: | ->c
* any_greater_power | for every great power: | ->c
c sphere_owner | for the great power whose sphere the nation is in: | ->c
c overlord | for the nation's overlord: | ->c
c cultural_union | for the union nation of its culture group: | ->c
p owner | for the province's owner (a nation): | ->c
p controller | for the nation that holds the province: | ->c
p state_scope | for the province's whole state: | ->p
p any_neighbor_province | for every province next to it: | ->p
p random_neighbor_province | for one province next to it, picked at random: | ->p
p random_empty_neighbor_province | for one unowned province next to it, picked at random: | ->p
* random_province | for one province of the world, picked at random: | ->p
s location | for the province the pop lives in: | ->p
s country | for the pop's nation: | ->c
* THIS | for the nation this event is about (THIS): | ->c
* FROM | for the nation that sent it (FROM): | ->c
)CATALOGUE";
static const char *const CATALOGUE_GOALS =
    R"CATALOGUE(
# War goals
* attacker_goal | the attacker wants: {casus_belli} (about the state of province {state_province_id} — for the nation {country}) | {casus_belli:cb state_province_id?:prov country?:tag}
* defender_goal | the defender wants: {casus_belli} (about the state of province {state_province_id} — for the nation {country}) | {casus_belli:cb state_province_id?:prov country?:tag}
)CATALOGUE";
