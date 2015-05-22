/*
 *  Flo's Open libRary (floor)
 *  Copyright (C) 2004 - 2015 Florian Ziesche
 *  
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License only.
 *  
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *  
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "obj_loader.hpp"

//#define FLOOR_DEBUG_PARSER 1
//#define FLOOR_DEBUG_PARSER_SET_NAMES 1
#include <floor/lang/source_types.hpp>
#include <floor/lang/lang_context.hpp>
#include <floor/lang/grammar.hpp>
#include <floor/constexpr/const_string.hpp>
#include <floor/core/core.hpp>
#include <floor/threading/task.hpp>

#if defined(__APPLE__)
#include <SDL2_image/SDL_image.h>
#elif defined(__WINDOWS__)
#include <SDL2/SDL2_image.h>
#else
#include <SDL_image.h>
#endif

#if !defined(GL_BGRA8)
#define GL_BGRA8 GL_BGRA
#endif
#if !defined(GL_BGR8)
#define GL_BGR8 GL_BGR
#endif
			
#define __TEXTURE_FORMATS(F, src_surface, dst_internal_format, dst_format, dst_type) \
F(src_surface, dst_internal_format, dst_format, dst_type, GL_R8, GL_RED, GL_UNSIGNED_BYTE, 8, 0, 0, 0, 0) \
F(src_surface, dst_internal_format, dst_format, dst_type, GL_RG8, GL_RG, GL_UNSIGNED_BYTE, 16, 0, 8, 0, 0) \
F(src_surface, dst_internal_format, dst_format, dst_type, GL_RGB8, GL_RGB, GL_UNSIGNED_BYTE, 24, 0, 8, 16, 0) \
F(src_surface, dst_internal_format, dst_format, dst_type, GL_RGB8, GL_RGBA, GL_UNSIGNED_BYTE, 32, 0, 8, 16, 0) \
F(src_surface, dst_internal_format, dst_format, dst_type, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, 32, 0, 8, 16, 24) \
F(src_surface, dst_internal_format, dst_format, dst_type, GL_BGR8, GL_BGR, GL_UNSIGNED_BYTE, 24, 16, 8, 0, 0) \
F(src_surface, dst_internal_format, dst_format, dst_type, GL_BGR8, GL_BGRA, GL_UNSIGNED_BYTE, 32, 16, 8, 0, 0) \
F(src_surface, dst_internal_format, dst_format, dst_type, GL_BGRA8, GL_BGRA, GL_UNSIGNED_BYTE, 32, 16, 8, 0, 24) \
F(src_surface, dst_internal_format, dst_format, dst_type, GL_R16, GL_RED, GL_UNSIGNED_SHORT, 16, 0, 0, 0, 0) \
F(src_surface, dst_internal_format, dst_format, dst_type, GL_RG16, GL_RG, GL_UNSIGNED_SHORT, 32, 0, 16, 0, 0) \
F(src_surface, dst_internal_format, dst_format, dst_type, GL_RGB16, GL_RGB, GL_UNSIGNED_SHORT, 48, 0, 16, 32, 0) \
F(src_surface, dst_internal_format, dst_format, dst_type, GL_RGBA16, GL_RGBA, GL_UNSIGNED_SHORT, 64, 0, 16, 32, 48)

#define __CHECK_FORMAT(src_surface, dst_internal_format, dst_format, dst_type, \
					   gl_internal_format, gl_format, gl_type, bpp, rshift, gshift, bshift, ashift) \
if(src_surface->format->Rshift == rshift && \
   src_surface->format->Gshift == gshift && \
   src_surface->format->Bshift == bshift && \
   src_surface->format->Ashift == ashift && \
   src_surface->format->BitsPerPixel == bpp) { \
	dst_internal_format = gl_internal_format; \
	dst_format = gl_format; \
	dst_type = gl_type; \
}

#define check_format(surface, internal_format, format, texture_type) { \
	__TEXTURE_FORMATS(__CHECK_FORMAT, surface, internal_format, format, texture_type); \
}

class obj_lexer final {
public:
	//! phase 1: replaces \r occurrences and creates a newline iterator set
	static void map_characters(translation_unit& tu);
	//! phase 3+: the actual lexing (and all the other necessary stuff)
	static void lex(translation_unit& tu);
	
	//! for debugging and assignment purposes
	static void print_tokens(const translation_unit& tu);
	
	//! returns the iters corresponding <line number, column number>
	static pair<uint32_t, uint32_t> get_line_and_column_from_iter(const translation_unit& tu,
																  const source_iterator& iter);
	
	//! assigns the resp. FLOOR_KEYWORD and FLOOR_PUNCTUATOR enums/sub-type to the token type
	static void assign_token_sub_types(translation_unit& tu);
	
protected:
	// NOTE: these are all the functions to lex any token
	// NOTE: every lex_* function has to return an iterator to the character following the lexed token (or .end())
	// and must also set the iter parameter to this iterator position!
	
	static source_iterator lex_keyword_or_identifier(source_iterator& iter, const source_iterator& source_end);
	static source_iterator lex_decimal_constant(source_iterator& iter, const source_iterator& source_end);
	static source_iterator lex_comment(source_iterator& iter, const source_iterator& source_end);
	
	//! checks whether a single character is part of an escape sequence (char after '\')
	static bool is_escape_sequence_char(const source_iterator& iter);
	
	//! checks whether a single character is part of the source character set
	static bool is_char_in_character_set(const source_iterator& iter);
	
	//! checks whether a single character is printable
	static bool is_printable_char(const source_iterator& iter);
	
	// static class
	obj_lexer(const obj_lexer&) = delete;
	~obj_lexer() = delete;
	obj_lexer& operator=(const obj_lexer&) = delete;
	
};
//! lexer_error exception (contains the source_iterator to the erroneous character and a hopefully meaningful error message)
class obj_lexer_error final : public exception {
protected:
	source_iterator iter;
	string error_msg;
public:
	obj_lexer_error(source_iterator iter_, const string& error_msg_) : iter(iter_), error_msg(error_msg_) {}
	const char* what() const noexcept override;
	const source_iterator& get_iter() const noexcept;
};
const char* obj_lexer_error::what() const noexcept { return error_msg.c_str(); }
const source_iterator& obj_lexer_error::get_iter() const noexcept { return iter; }

//! simple TOKEN_TYPE enum to token name string conversion
static constexpr const char* token_type_to_string(const SOURCE_TOKEN_TYPE& token_type) {
	switch(token_type) {
		case SOURCE_TOKEN_TYPE::KEYWORD: return "keyword";
		case SOURCE_TOKEN_TYPE::IDENTIFIER: return "identifier";
		case SOURCE_TOKEN_TYPE::CONSTANT: return "constant";
		case SOURCE_TOKEN_TYPE::STRING_LITERAL: return "string-literal";
		case SOURCE_TOKEN_TYPE::PUNCTUATOR: return "punctuator";
		case SOURCE_TOKEN_TYPE::INTEGER_CONSTANT: return "integer-constant";
		case SOURCE_TOKEN_TYPE::UNSIGNED_INTEGER_CONSTANT: return "unsigned-integer-constant";
		case SOURCE_TOKEN_TYPE::CHARACTER_CONSTANT: return "character-constant";
			
		case SOURCE_TOKEN_TYPE::INVALID:
		case SOURCE_TOKEN_TYPE::__BASE_TYPE_MASK:
		case SOURCE_TOKEN_TYPE::__SUB_TYPE_MASK:
			return "<invalid token type>";
	}
}

//! contains all valid punctuators
static const unordered_map<string, FLOOR_PUNCTUATOR> punctuator_tokens {
	{ "/", FLOOR_PUNCTUATOR::DIV },
};

void obj_lexer::map_characters(translation_unit& tu) {
	// -> we will only need to remove \r characters here (replace \r\n by \n and replace single \r chars by \n)
	// also, while we're at it, also build a "lines set" (iterator to each line)
	// TODO: \r actually doesn't have to be replaced (just skip like \n + don't count as newline if \r\n?)
	vector<uint32_t> lines; // NOTE: we can't store iterators yet, so this must be a uint
	for(auto begin_iter = begin(tu.source), end_iter = end(tu.source), iter = begin_iter; iter != end_iter; ++iter) {
		if(*iter == '\n' || *iter == '\r') {
			if(*iter == '\r') {
				auto next_iter = iter + 1;
				if(next_iter != end_iter && *next_iter == '\n') {
					// replace \r\n with single \n (erase \r)
					iter = tu.source.erase(iter); // iter now at '\n'
					// we now have a new end and begin iter
					end_iter = end(tu.source);
					begin_iter = begin(tu.source);
				}
				else {
					// single \r -> \n replace
					*iter = '\n';
				}
			}
			// else: \n
			
			// add newline position
			lines.emplace_back(distance(begin_iter, iter));
		}
	}
	
	// TODO: don't use a set, rather use a vector (it is already sorted!) and binary_search
	// now that we can store iterators, do so (+store it in an actual set<>)
	const auto begin_iter = tu.source.cbegin();
	// add the "character before the first character" as a newline
	// (yes, kind of a hack, but this keeps us from doing +1s/-1s and begin/end checking later on)
	tu.lines.insert(tu.source.cbegin() - 1);
	// add all newline iterators (offset from the begin iterator)
	for(const auto& line_offset : lines) {
		tu.lines.insert(begin_iter + line_offset);
	}
	// also insert the "<eof> newline" (if it hasn't been added already)
	tu.lines.insert(tu.source.cend());
	// NOTE: the additional begin+end newline iterators will make sure that there will always be
	// a valid line iterator for each source_iterator (all tokens)
}

void obj_lexer::lex(translation_unit& tu) {
	// tokens reserve strategy: "4 chars : 1 token" seems like a good ratio for now
	tu.tokens.reserve(tu.source.size() / 4);
	
	// lex
	try {
		for(auto char_iter = tu.source.cbegin(), src_end = tu.source.cend();
			char_iter != src_end;
			/* NOTE: char_iter is incremented in the individual lex_* functions or whitespace case: */) {
			switch(*char_iter) {
				// keyword or identifier
				case '_':
				case 'a': case 'b': case 'c': case 'd':
				case 'e': case 'f': case 'g': case 'h':
				case 'i': case 'j': case 'k': case 'l':
				case 'm': case 'n': case 'o': case 'p':
				case 'q': case 'r': case 's': case 't':
				case 'u': case 'v': case 'w': case 'x':
				case 'y': case 'z':
				case 'A': case 'B': case 'C': case 'D':
				case 'E': case 'F': case 'G': case 'H':
				case 'I': case 'J': case 'K': case 'L':
				case 'M': case 'N': case 'O': case 'P':
				case 'Q': case 'R': case 'S': case 'T':
				case 'U': case 'V': case 'W': case 'X':
				case 'Y': case 'Z': {
					source_range range { char_iter, char_iter };
					range.end = lex_keyword_or_identifier(char_iter, src_end);
					tu.tokens.emplace_back(SOURCE_TOKEN_TYPE::IDENTIFIER, range);
					break;
				}
				
				// decimal constant
				case '-': case '.':
				case '0': case '1': case '2': case '3':
				case '4': case '5': case '6': case '7':
				case '8': case '9': {
					source_range range { char_iter, char_iter };
					range.end = lex_decimal_constant(char_iter, src_end);
					tu.tokens.emplace_back(SOURCE_TOKEN_TYPE::INTEGER_CONSTANT, range);
					break;
				}
				
				// '#' -> comment
				case '#': {
					// comment
					lex_comment(char_iter, src_end);
					break;
				}
				
				// '/' -> separator/punctuator
				case '/': {
					source_range range { char_iter, char_iter + 1 };
					++char_iter;
					tu.tokens.emplace_back(SOURCE_TOKEN_TYPE::PUNCTUATOR, range);
					break;
				}
				
				// whitespace
				// "space, horizontal tab, new-line, vertical tab, and form-feed"
				case ' ': case '\t': case '\n': case '\v':
				case '\f':
					// continue
					++char_iter;
					break;
					
				// invalid char
				default: {
					const string invalid_char = (is_printable_char(char_iter) ? string(1, *char_iter) : "<unprintable>");
					throw obj_lexer_error(char_iter, "invalid character \'" + invalid_char + "\' (" +
										  to_string(0xFFu & (uint32_t)*char_iter) + ")");
				}
			}
		}
	}
	catch(obj_lexer_error& err) {
		// print the error (<file>:<line>:<column>: error: <error-text>)
		const auto line_and_column = get_line_and_column_from_iter(tu, err.get_iter());
		log_error("%s:%u:%u: error: %s",
				  tu.file_name, line_and_column.first, line_and_column.second, err.what());
	}
	catch(exception& exc) {
		log_error("uncaught exception during lexing of file \"%s\": %s",
				  tu.file_name, exc.what());
	}
	catch(...) {
		log_error("uncaught exception during lexing of file \"%s\"",
				  tu.file_name);
	}
}

void obj_lexer::print_tokens(const translation_unit& tu) {
	// NOTE: for the sake of speed, this uses fwrite instead of cout and string voodoo ...
	// also: doesn't use get_line_and_column_from_iter(...), because this would mean quadratic runtime
	// instead: just keep track of the newline iters and use a line counter
	string tmp;
	tmp.reserve(256);
	tmp = tu.file_name + ":";
	const auto insert_loc = tmp.size();
	
	const auto lines_begin = tu.lines.cbegin();
	auto cur_line = lines_begin, next_line = next(lines_begin);
	uint32_t line_num = 1;
	
	for(const auto& token : tu.tokens) {
		// <file>:<line>:<column>: <token-type> <token-text>
		tmp.erase(insert_loc);
		
		// current token iter
		const auto& tok_begin = token.second.begin;
		
		// get/update current and next line iter and line number
		while(tok_begin >= *next_line) {
			cur_line = next_line;
			++next_line;
			++line_num;
		}
		
		// compute column num (distance between last newline and current token)
		const uint32_t column_num = (uint32_t)distance(*cur_line, tok_begin);
		
		tmp.append(to_string(line_num));
		tmp.append(":");
		tmp.append(to_string(column_num));
		tmp.append(": ");
		tmp.append(token_type_to_string(token.first));
		tmp.append(" ");
		tmp.append(token.second.begin, token.second.end);
		tmp.append("\n");
		fwrite(tmp.data(), 1, tmp.size(), stdout);
	}
}

source_iterator obj_lexer::lex_keyword_or_identifier(source_iterator& iter, const source_iterator& source_end) {
	for(++iter; iter != source_end; ++iter) {
		switch(*iter) {
			// valid keyword and identifier characters
			case '_': case '.': case '/': case '\\':
			case '-':
			case 'a': case 'b': case 'c': case 'd':
			case 'e': case 'f': case 'g': case 'h':
			case 'i': case 'j': case 'k': case 'l':
			case 'm': case 'n': case 'o': case 'p':
			case 'q': case 'r': case 's': case 't':
			case 'u': case 'v': case 'w': case 'x':
			case 'y': case 'z':
			case 'A': case 'B': case 'C': case 'D':
			case 'E': case 'F': case 'G': case 'H':
			case 'I': case 'J': case 'K': case 'L':
			case 'M': case 'N': case 'O': case 'P':
			case 'Q': case 'R': case 'S': case 'T':
			case 'U': case 'V': case 'W': case 'X':
			case 'Y': case 'Z':
			case '0': case '1': case '2': case '3':
			case '4': case '5': case '6': case '7':
			case '8': case '9':
				continue;
				
			// anything else -> done, return end iter
			default:
				return iter;
		}
	}
	return iter; // eof
}

source_iterator obj_lexer::lex_decimal_constant(source_iterator& iter, const source_iterator& source_end) {
	for(++iter; iter != source_end; ++iter) {
		switch(*iter) {
			// valid decimal constant characters
			case '.': case '-': case '+':
			case 'e': case 'E':
			case '0': case '1': case '2': case '3':
			case '4': case '5': case '6': case '7':
			case '8': case '9':
				continue;
				
			// anything else -> done, return end iter
			default:
				return iter;
		}
	}
	return iter; // eof
}

source_iterator obj_lexer::lex_comment(source_iterator& iter, const source_iterator& source_end) {
	// NOTE: we already made sure that this must be a comment
	++iter;
	
	// single-line comment
	for(++iter; iter != source_end; ++iter) {
		// newline signals end of single-line comment
		if(*iter == '\n') {
			return iter;
		}
	}
	return iter; // eof is okay in a single-line comment
}

bool obj_lexer::is_char_in_character_set(const source_iterator& iter) {
	// http://en.wikipedia.org/wiki/ISO/IEC_8859-1#Codepage_layout
	// -> valid characters are in the ranges [0x20, 0x7E] and [0xA0, 0xFF]
	// -> valid control characters are everything in [0x00, 0x1F] except 0x00, 0x0A and 0x0D (terminator, newline, carriage return)
	// NOTE: don't use characters here, since the encoding of *this file is utf-8, not iso-8859-1!
	const uint8_t char_uint = (uint8_t)*iter;
	return ((char_uint >= 0x01 && char_uint <= 0x09) ||
			(char_uint >= 0x0B && char_uint <= 0x0C) ||
			(char_uint >= 0x0E && char_uint <= 0x7E) ||
			(char_uint >= 0xA0 /* && <= 0xFF, always true */));
}

bool obj_lexer::is_printable_char(const source_iterator& iter) {
	// -> see obj_lexer::is_char_in_character_set
	// this will only accept 0x09 (tab), [0x20, 0x7E] and [0xA0, 0xFF]
	const uint8_t char_uint = (uint8_t)*iter;
	return ((char_uint == 0x09) ||
			(char_uint >= 0x20 && char_uint <= 0x7E) ||
			(char_uint >= 0xA0 /* && <= 0xFF, always true */));
}

void obj_lexer::assign_token_sub_types(translation_unit& tu) {
	for(auto& token : tu.tokens) {
		// skip non-punctuators
		if(token.first != SOURCE_TOKEN_TYPE::PUNCTUATOR) {
			continue;
		}
		
		// handle punctuators
		token.first |= (SOURCE_TOKEN_TYPE)punctuator_tokens.find(token.second.to_string())->second;
	}
}

pair<uint32_t, uint32_t> obj_lexer::get_line_and_column_from_iter(const translation_unit& tu,
																  const source_iterator& iter) {
	// eof check
	if(iter == *tu.lines.cend()) {
		return { 0, 0 };
	}
	
	pair<uint32_t, uint32_t> ret;
	const auto lines_begin = tu.lines.cbegin();
	
	// line num
	const auto line_iter = tu.lines.lower_bound(iter);
	ret.first = (uint32_t)distance(lines_begin, line_iter);
	
	// column num
	const auto prev_newline = *prev(line_iter);
	ret.second = (uint32_t)distance(prev_newline, iter);
	
	return ret;
}

struct keyword_matcher : public parser_node_base<keyword_matcher> {
	const char* keyword;
	const size_t keyword_len;
	
	template <size_t len> constexpr keyword_matcher(const char (&keyword_)[len]) noexcept :
	keyword(keyword_), keyword_len(len - 1 /* -1, b/c of \0 */) {}
	
	match_return_type match(parser_context& ctx) const {
#if defined(FLOOR_DEBUG_PARSER) && !defined(FLOOR_DEBUG_PARSER_MATCHES_ONLY)
		ctx.print_at_depth("matching KEYWORD");
#endif
		if(!ctx.at_end() &&
		   ctx.iter->first == SOURCE_TOKEN_TYPE::IDENTIFIER &&
		   ctx.iter->second.equal(keyword, keyword_len)) {
			match_return_type ret { ctx.iter };
			ctx.next();
			return ret;
		}
		return { false, false, {} };
	}
};

struct mtl_grammar {
	// all grammar rule objects
#if !defined(FLOOR_DEBUG_PARSER) && !defined(FLOOR_DEBUG_PARSER_SET_NAMES)
#define FLOOR_MTL_GRAMMAR_OBJECTS(...) grammar_rule __VA_ARGS__;
#else
#define FLOOR_MTL_GRAMMAR_OBJECTS(...) grammar_rule __VA_ARGS__; \
	/* in debug mode, also set the debug name of each grammar rule object */ \
	void set_debug_names() { \
		string names { #__VA_ARGS__ }; \
		set_debug_name(names, __VA_ARGS__); \
	} \
	void set_debug_name(string& names, grammar_rule& obj) { \
		const auto comma_pos = names.find(","); \
		obj.debug_name = names.substr(0, comma_pos); \
		names.erase(0, comma_pos + 1 + (comma_pos+1 < names.size() && names[comma_pos+1] == ' ' ? 1 : 0)); \
	} \
	template <typename... grammar_objects> void set_debug_name(string& names, grammar_rule& obj, grammar_objects&... objects) { \
		set_debug_name(names, obj); \
		set_debug_name(names, objects...); \
	}
#endif
	
	FLOOR_MTL_GRAMMAR_OBJECTS(global_scope, statement, newmtl,
							  ns, ni, d, tr, tf, illum,
							  ka, kd, ks, ke,
							  map_ka, map_kd, map_ks, map_bump, map_d)
	
	struct obj_material {
		string ambient { "" };
		string diffuse { "" };
		string specular { "" };
		string normal { "" };
		string mask { "" };
	};
	unordered_map<string, unique_ptr<obj_material>> materials;
	obj_material* cur_mtl { nullptr };
	
	mtl_grammar() {
		// fixed token type matchers:
		static constexpr literal_matcher<const char*, SOURCE_TOKEN_TYPE::INTEGER_CONSTANT> FP_CONSTANT {};
		static constexpr literal_matcher<const char*, SOURCE_TOKEN_TYPE::IDENTIFIER> IDENTIFIER {};
		
		//
		static constexpr keyword_matcher NEWMTL("newmtl"), NS("Ns"), NI("Ni"), D("d"), TR("Tr"), TF("Tf"), ILLUM("illum"), KA("Ka"), KD("Kd"), KS("Ks"), KE("Ke"), MAP_KA("map_Ka"), MAP_KD("map_Kd"), MAP_KS("map_Ks"), MAP_BUMP("map_bump"), BUMP("bump"), MAP_D("map_d");
		static constexpr literal_matcher<FLOOR_PUNCTUATOR, SOURCE_TOKEN_TYPE::PUNCTUATOR> SLASH { FLOOR_PUNCTUATOR::DIV };
		
#if defined(FLOOR_DEBUG_PARSER) || defined(FLOOR_DEBUG_PARSER_SET_NAMES)
		set_debug_names();
#endif
		
		// grammar:
		// ref: http://paulbourke.net/dataformats/mtl/
		global_scope = *statement;
		statement = (newmtl | ns | ni | d | tr | tf | illum | ka | kd | ks | ke | map_ka | map_kd | map_ks | map_bump | map_d);
		newmtl = NEWMTL & IDENTIFIER;
		ns = NS & FP_CONSTANT;
		ni = NI & FP_CONSTANT;
		d = D & FP_CONSTANT;
		tr = TR & FP_CONSTANT;
		tf = TF & FP_CONSTANT & FP_CONSTANT & FP_CONSTANT;
		illum = ILLUM & FP_CONSTANT;
		ka = KA & FP_CONSTANT & FP_CONSTANT & FP_CONSTANT;
		kd = KD & FP_CONSTANT & FP_CONSTANT & FP_CONSTANT;
		ks = KS & FP_CONSTANT & FP_CONSTANT & FP_CONSTANT;
		ke = KE & FP_CONSTANT & FP_CONSTANT & FP_CONSTANT;
		map_ka = MAP_KA & IDENTIFIER;
		map_kd = MAP_KD & IDENTIFIER;
		map_ks = MAP_KS & IDENTIFIER;
		map_bump = (MAP_BUMP | BUMP) & IDENTIFIER;
		map_d = MAP_D & IDENTIFIER;
		
		//
		newmtl.on_match([this](auto& matches) -> parser_context::match_list {
			const auto name = matches[1].token->second.to_string();
			auto mtl = make_unique<obj_material>();
			materials.emplace(name, move(mtl));
			cur_mtl = materials[name].get();
			return {};
		});
		map_ka.on_match([this](auto& matches) -> parser_context::match_list {
			cur_mtl->ambient = matches[1].token->second.to_string();
			return {};
		});
		map_kd.on_match([this](auto& matches) -> parser_context::match_list {
			cur_mtl->diffuse = matches[1].token->second.to_string();
			return {};
		});
		map_ks.on_match([this](auto& matches) -> parser_context::match_list {
			cur_mtl->specular = matches[1].token->second.to_string();
			return {};
		});
		map_bump.on_match([this](auto& matches) -> parser_context::match_list {
			cur_mtl->normal = matches[1].token->second.to_string();
			return {};
		});
		map_d.on_match([this](auto& matches) -> parser_context::match_list {
			cur_mtl->mask = matches[1].token->second.to_string();
			return {};
		});
	}
	
	unordered_map<string, unique_ptr<obj_material>> parse(parser_context& ctx, bool& success) {
		// clear all
		materials.clear();
		success = false;
		
		// parse
		global_scope.match(ctx);
		
		// if the end hasn't been reached, we have an error
		if(ctx.iter != ctx.end) {
			string error_msg = "parsing failed: ";
			if(ctx.deepest_iter == ctx.tu.tokens.cend()) {
				error_msg += "premature EOF after";
				ctx.deepest_iter = ctx.end - 1; // set iter to token before EOF
			}
			else {
				error_msg += "possibly at";
			}
			error_msg += " \"" + ctx.deepest_iter->second.to_string() + "\"";
			
			const auto line_and_column = obj_lexer::get_line_and_column_from_iter(ctx.tu, ctx.deepest_iter->second.begin);
			log_error("%s:%u:%u: %s",
					  ctx.tu.file_name, line_and_column.first, line_and_column.second, error_msg);
			return {};
		}
		log_debug("mtl parsing done");
		
		// done
		success = true;
		return move(materials);
	}
};


static void load_textures(// file name -> gl tex id
						  unordered_map<string, uint32_t>& texture_filenames,
						  // gl tex ids
						  vector<GLuint>& model_textures,
						  // path prefix
						  const string& prefix) {
	// load textures
	const auto filenames = core::keys(texture_filenames);
	alignas(128) vector<SDL_Surface*> surfaces(filenames.size(), nullptr);
	atomic<uint32_t> cur_tex { 0u }, active_workers { core::get_hw_thread_count() };
	for(uint32_t worker_id = 0; worker_id < core::get_hw_thread_count(); ++worker_id) {
		task::spawn([worker_id, &active_workers, &filenames, &cur_tex, &surfaces, &prefix] {
			for(;;) {
				const auto id = cur_tex++;
				if(id >= filenames.size()) break;
				
				// look for path
				string filename = filenames[id]; // check path in .mtl first
				if(!file_io::is_file(filename)) {
					filename = floor::data_path(filenames[id]); // then, check data_path root
					if(!file_io::is_file(filename)) {
						filename = prefix + filenames[id]; // finally, check with prefix
						if(!file_io::is_file(filename)) {
							log_error("couldn't find texture \"%s\"!", filenames[id]);
							continue;
						}
					}
				}
				
				SDL_Surface* surface = IMG_Load(filename.c_str());
				if(surface == nullptr) {
					log_error("error loading texture file \"%s\": %s!", filenames[id], SDL_GetError());
					continue;
				}
				
				// check format, BGR(A) needs to be converted to RGB(A)
				GLint internal_format = 0;
				GLenum format = 0;
				GLenum type = 0;
				check_format(surface, internal_format, format, type);
				if(format == GL_BGR || format == GL_BGRA) {
					SDL_PixelFormat new_pformat;
					memcpy(&new_pformat, surface->format, sizeof(SDL_PixelFormat));
					new_pformat.Rshift = surface->format->Bshift;
					new_pformat.Bshift = surface->format->Rshift;
					new_pformat.Rmask = surface->format->Bmask;
					new_pformat.Bmask = surface->format->Rmask;
					
					bool alpha_component = (surface->format->Ashift != 0);
					if(!alpha_component) {
						new_pformat.BytesPerPixel = 3;
						new_pformat.BitsPerPixel = 24;
					}
					else if(format == GL_BGRA) {
						new_pformat.BytesPerPixel = 4;
						new_pformat.BitsPerPixel = 32;
					}
					
					SDL_Surface* new_surface = SDL_ConvertSurface(surface, &new_pformat, 0);
					if(new_surface == nullptr) {
						log_error("BGR(A)->RGB(A) surface conversion failed!");
						continue;
					}
					SDL_FreeSurface(surface);
					surface = new_surface;
				}
				
				surfaces[id] = surface;
			}
			active_workers--;
		});
	}
	while(active_workers != 0) {
		this_thread::sleep_for(10ms);
	}
	log_debug("%u textures loaded to mem", surfaces.size());
	
	// create gl textures
	model_textures.resize(surfaces.size());
	glGenTextures((GLsizei)model_textures.size(), &model_textures[0]);
	for(size_t i = 0, count = model_textures.size(); i < count; ++i) {
		const SDL_Surface* surface = surfaces[i];
		if(surface == nullptr) {
			log_debug("surface #%u invalid due to texture load failure - continuing ...", i);
			continue;
		}
		
		// assign gl tex id to tex filename
		texture_filenames[filenames[i]] = model_textures[i];
		
		// these values will be computed
		GLint internal_format = 0;
		GLenum format = 0;
		GLenum type = 0;
		check_format(surface, internal_format, format, type);
		
		//
		glBindTexture(GL_TEXTURE_2D, model_textures[i]);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, 16);
		glTexImage2D(GL_TEXTURE_2D, 0, internal_format, surface->w, surface->h, 0, format, type, surface->pixels);
		glGenerateMipmap(GL_TEXTURE_2D);
		
		// check for format problems
		const auto gl_error = glGetError();
		if(gl_error != 0) {
			log_error("gl error in texture #%u: %X", i, gl_error);
		}
	}
	log_debug("gl textures created");
}

struct obj_grammar {
	// all grammar rule objects
#if !defined(FLOOR_DEBUG_PARSER) && !defined(FLOOR_DEBUG_PARSER_SET_NAMES)
#define FLOOR_OBJ_GRAMMAR_OBJECTS(...) grammar_rule __VA_ARGS__;
#else
#define FLOOR_OBJ_GRAMMAR_OBJECTS(...) grammar_rule __VA_ARGS__; \
	/* in debug mode, also set the debug name of each grammar rule object */ \
	void set_debug_names() { \
		string names { #__VA_ARGS__ }; \
		set_debug_name(names, __VA_ARGS__); \
	} \
	void set_debug_name(string& names, grammar_rule& obj) { \
		const auto comma_pos = names.find(","); \
		obj.debug_name = names.substr(0, comma_pos); \
		names.erase(0, comma_pos + 1 + (comma_pos+1 < names.size() && names[comma_pos+1] == ' ' ? 1 : 0)); \
	} \
	template <typename... grammar_objects> void set_debug_name(string& names, grammar_rule& obj, grammar_objects&... objects) { \
		set_debug_name(names, obj); \
		set_debug_name(names, objects...); \
	}
#endif
	
	FLOOR_OBJ_GRAMMAR_OBJECTS(global_scope, statement,
							  vertex, tex_coord, normal,
							  face_3, face_4, index_triplet1, index_triplet2, index_triplet2_vn, index_triplet3,
							  sub_object,
							  smooth,
							  mtllib, usemtl)
	
	struct obj_face {
		uint4 vertex;
		uint4 tex_coord;
		uint4 normal;
	};
	
	struct obj_sub_object {
		const string name;
		string mat;
		vector<obj_face> indices;
	};
	
	vector<float4> vertices;
	vector<float3> tex_coords;
	vector<float3> normals;
	vector<unique_ptr<obj_sub_object>> sub_objects;
	obj_sub_object* cur_obj { nullptr };
	string mat_filename { "" };
	
	obj_grammar() {
		// fixed token type matchers:
		static constexpr literal_matcher<const char*, SOURCE_TOKEN_TYPE::INTEGER_CONSTANT> FP_CONSTANT {};
		static constexpr literal_matcher<const char*, SOURCE_TOKEN_TYPE::IDENTIFIER> IDENTIFIER {};
		
		//
		static constexpr keyword_matcher VERTEX("v"), FACE("f"), TEX_COORD("vt"), NORMAL("vn"), SUB_OBJECT("g"), MTLLIB("mtllib"), USEMTL("usemtl"), SMOOTH("s"), SMOOTH_OFF("off");
		static constexpr literal_matcher<FLOOR_PUNCTUATOR, SOURCE_TOKEN_TYPE::PUNCTUATOR> SLASH { FLOOR_PUNCTUATOR::DIV };
		
#if defined(FLOOR_DEBUG_PARSER) || defined(FLOOR_DEBUG_PARSER_SET_NAMES)
		set_debug_names();
#endif
		
		// grammar:
		// ref: http://www.martinreddy.net/gfx/3d/OBJ.spec
		global_scope = *statement;
		statement = (vertex | tex_coord | normal | face_4 | face_3 | sub_object | smooth | mtllib | usemtl);
		vertex = VERTEX & FP_CONSTANT & FP_CONSTANT & FP_CONSTANT & ~FP_CONSTANT;
		tex_coord = TEX_COORD & FP_CONSTANT & ~(FP_CONSTANT & ~FP_CONSTANT);
		normal = NORMAL & FP_CONSTANT & FP_CONSTANT & FP_CONSTANT;
		sub_object = SUB_OBJECT & IDENTIFIER;
		smooth = SMOOTH & (FP_CONSTANT | SMOOTH_OFF);
		mtllib = MTLLIB & IDENTIFIER;
		usemtl = USEMTL & IDENTIFIER;
		
		// there are valid 4 possibilities
		// note that the spec requires that all triplets in a face have the same layout!
		// f # # #
		// f #/# #/# #/#
		// f #//# #//# #//#
		// f #/#/# #/#/# #/#/#
		index_triplet1 = FP_CONSTANT;
		index_triplet2 = FP_CONSTANT & SLASH & FP_CONSTANT;
		index_triplet2_vn = FP_CONSTANT & SLASH & SLASH & FP_CONSTANT;
		index_triplet3 = FP_CONSTANT & SLASH & FP_CONSTANT & SLASH & FP_CONSTANT;
		
		face_4 = FACE & ((index_triplet3 & index_triplet3 & index_triplet3 & index_triplet3) |
				  (index_triplet2_vn & index_triplet2_vn & index_triplet2_vn & index_triplet2_vn) |
				  (index_triplet2 & index_triplet2 & index_triplet2 & index_triplet2) |
				  (index_triplet1 & index_triplet1 & index_triplet1 & index_triplet1));
		
		face_3 = FACE & ((index_triplet3 & index_triplet3 & index_triplet3) |
				  (index_triplet2_vn & index_triplet2_vn & index_triplet2_vn) |
				  (index_triplet2 & index_triplet2 & index_triplet2) |
				  (index_triplet1 & index_triplet1 & index_triplet1));
		
		// match handling
		static const auto match_to_float = [](const parser_context::match& match) {
			return stof(match.token->second.to_string());
		};
		static const auto match_to_uint = [](const parser_context::match& match) {
			return stou(match.token->second.to_string());
		};
		static const auto push_to_parent = [](parser_context::match_list& matches) -> parser_context::match_list {
			return move(matches);
		};
		
		vertex.on_match([this](auto& matches) -> parser_context::match_list {
			vertices.emplace_back(match_to_float(matches[1]),
								  match_to_float(matches[2]),
								  match_to_float(matches[3]),
								  (matches.size() > 4 ? match_to_float(matches[4]) : 1.0f));
			return {};
		});
		tex_coord.on_match([this](auto& matches) -> parser_context::match_list {
			tex_coords.emplace_back(match_to_float(matches[1]),
									// always flip y ...
									(matches.size() > 2 ? 1.0f - match_to_float(matches[2]) : 0.0f),
									(matches.size() > 3 ? match_to_float(matches[3]) : 0.0f));
			return {};
		});
		normal.on_match([this](auto& matches) -> parser_context::match_list {
			normals.emplace_back(match_to_float(matches[1]),
								 match_to_float(matches[2]),
								 match_to_float(matches[3]));
			return {};
		});
		face_3.on_match([this](parser_context::match_list& matches) -> parser_context::match_list {
			const size_t count = matches.size();
			size_t type = 0;
			switch(count) {
				case 4: type = 0; break;
				case 10: type = 1; break;
				case 13: type = 2; break;
				case 16: type = 3; break;
				default:
					log_error("face_3: invalid match count: %u", count);
					return {};
			}
			
			static const array<uint3, 4> v_offsets {{
				{ 1, 2, 3 },
				{ 1, 4, 7 },
				{ 1, 5, 9 },
				{ 1, 6, 11 },
			}};
			static const array<uint3, 4> tc_offsets {{
				{ 0, 0, 0 },
				{ 3, 6, 9 },
				{ 0, 0, 0 },
				{ 3, 8, 13 },
			}};
			static const array<uint3, 4> n_offsets {{
				{ 0, 0, 0 },
				{ 0, 0, 0 },
				{ 4, 8, 12 },
				{ 5, 10, 15 },
			}};
			
			const auto& v_offset = v_offsets[type];
			const auto& tc_offset = tc_offsets[type];
			const auto& n_offset = n_offsets[type];
			
			cur_obj->indices.emplace_back(obj_face {
				uint4 {
					match_to_uint(matches[v_offset.x]),
					match_to_uint(matches[v_offset.y]),
					match_to_uint(matches[v_offset.z]),
					0
				},
				uint4 {
					tc_offset.x == 0 ? 0 : match_to_uint(matches[tc_offset.x]),
					tc_offset.y == 0 ? 0 : match_to_uint(matches[tc_offset.y]),
					tc_offset.z == 0 ? 0 : match_to_uint(matches[tc_offset.z]),
					0
				},
				uint4 {
					n_offset.x == 0 ? 0 : match_to_uint(matches[n_offset.x]),
					n_offset.y == 0 ? 0 : match_to_uint(matches[n_offset.y]),
					n_offset.z == 0 ? 0 : match_to_uint(matches[n_offset.z]),
					0
				}
			});
			return {};
		});
		face_4.on_match([this](auto& matches) -> parser_context::match_list {
			const size_t count = matches.size();
			size_t type = 0;
			switch(count) {
				case 5: type = 0; break;
				case 13: type = 1; break;
				case 17: type = 2; break;
				case 21: type = 3; break;
				default:
					log_error("face_4: invalid match count: %u", count);
					return {};
			}
			
			static const array<uint4, 4> v_offsets {{
				{ 1, 2, 3, 4 },
				{ 1, 4, 7, 10 },
				{ 1, 5, 9, 13 },
				{ 1, 6, 11, 16 },
			}};
			static const array<uint4, 4> tc_offsets {{
				{ 0, 0, 0, 0 },
				{ 3, 6, 9, 12 },
				{ 0, 0, 0, 0 },
				{ 3, 8, 13, 18 },
			}};
			static const array<uint4, 4> n_offsets {{
				{ 0, 0, 0, 0 },
				{ 0, 0, 0, 0 },
				{ 4, 8, 12, 16 },
				{ 5, 10, 15, 20 },
			}};
			
			const auto& v_offset = v_offsets[type];
			const auto& tc_offset = tc_offsets[type];
			const auto& n_offset = n_offsets[type];
			
			cur_obj->indices.emplace_back(obj_face {
				uint4 {
					match_to_uint(matches[v_offset.x]),
					match_to_uint(matches[v_offset.y]),
					match_to_uint(matches[v_offset.z]),
					match_to_uint(matches[v_offset.w])
				},
				uint4 {
					tc_offset.x == 0 ? 0 : match_to_uint(matches[tc_offset.x]),
					tc_offset.y == 0 ? 0 : match_to_uint(matches[tc_offset.y]),
					tc_offset.z == 0 ? 0 : match_to_uint(matches[tc_offset.z]),
					tc_offset.w == 0 ? 0 : match_to_uint(matches[tc_offset.w])
				},
				uint4 {
					n_offset.x == 0 ? 0 : match_to_uint(matches[n_offset.x]),
					n_offset.y == 0 ? 0 : match_to_uint(matches[n_offset.y]),
					n_offset.z == 0 ? 0 : match_to_uint(matches[n_offset.z]),
					n_offset.w == 0 ? 0 : match_to_uint(matches[n_offset.w])
				}
			});
			return {};
		});
		index_triplet1.on_match(push_to_parent);
		index_triplet2.on_match(push_to_parent);
		index_triplet2_vn.on_match(push_to_parent);
		index_triplet3.on_match(push_to_parent);
		sub_object.on_match([this](auto& matches) -> parser_context::match_list {
			auto obj = make_unique<obj_sub_object>(obj_sub_object { .name = matches[1].token->second.to_string() });
			sub_objects.emplace_back(move(obj));
			cur_obj = sub_objects.back().get();
			return {};
		});
		mtllib.on_match([this](auto& matches) -> parser_context::match_list {
			mat_filename = matches[1].token->second.to_string();
			return {};
		});
		usemtl.on_match([this](auto& matches) -> parser_context::match_list {
			// if a material has already been assigned, a new sub-object must be created
			if(cur_obj->mat != "") {
				auto obj = make_unique<obj_sub_object>(obj_sub_object { .name = cur_obj->name + "#" });
				sub_objects.emplace_back(move(obj));
				cur_obj = sub_objects.back().get();
			}
			cur_obj->mat = matches[1].token->second.to_string();
			return {};
		});
	}
	
	obj_model parse(parser_context& ctx, bool& success) {
		// clear all
		vertices.clear();
		tex_coords.clear();
		normals.clear();
		sub_objects.clear();
		cur_obj = nullptr;
		mat_filename = "";
		success = false;
		
		// parse
		global_scope.match(ctx);
		
		// if the end hasn't been reached, we have an error
		if(ctx.iter != ctx.end) {
			string error_msg = "parsing failed: ";
			if(ctx.deepest_iter == ctx.tu.tokens.cend()) {
				error_msg += "premature EOF after";
				ctx.deepest_iter = ctx.end - 1; // set iter to token before EOF
			}
			else {
				error_msg += "possibly at";
			}
			error_msg += " \"" + ctx.deepest_iter->second.to_string() + "\"";
			
			const auto line_and_column = obj_lexer::get_line_and_column_from_iter(ctx.tu, ctx.deepest_iter->second.begin);
			log_error("%s:%u:%u: %s",
					  ctx.tu.file_name, line_and_column.first, line_and_column.second, error_msg);
			return {};
		}
		log_debug("parsing done");
		
		// create proper model
		obj_model model;
		model.vertices.reserve(vertices.size());
		model.tex_coords.reserve(vertices.size());
		model.normals.reserve(vertices.size());
		model.binormals.reserve(vertices.size());
		model.tangents.reserve(vertices.size());
		model.objects.reserve(sub_objects.size());
		log_debug("alloc #0");
		
		struct reassign_entry {
			struct target {
				uint32_t tc_idx { 0 };
				uint32_t n_idx { 0 };
				uint32_t dst_idx { 0 };
			};
			uint32_t used { 0 };
			vector<target> targets;
			
			// start off with 4 everywhere (should be enough for most models, otherwise huge realloc will ensue ...)
			reassign_entry() : targets(4) {}
			
			void add(const uint32_t& tc, const uint32_t& n, const uint32_t& dst) {
				if(used < targets.size()) {
					targets[used].tc_idx = tc;
					targets[used].n_idx = n;
					targets[used].dst_idx = dst;
					++used;
					return;
				}
				log_error("meh: %u: %u %u", used, tc, n);
				targets.emplace_back(target { tc, n, dst });
				++used;
			}
		};
		vector<reassign_entry> index_reassign(vertices.size() + 1);
		log_debug("alloc #1");
		
		uint32_t index_counter = 1u; // start off at 1, 0 is the "invalid"/zero data
		static const auto reassign = [this, &model, &index_reassign, &index_counter](const uint32_t& v_idx,
																					 const uint32_t& tc_idx,
																					 const uint32_t& n_idx) {
			auto& entry = index_reassign[v_idx];
			for(uint32_t i = 0; i < entry.used; ++i) {
				if(entry.targets[i].tc_idx == tc_idx /*&&
				   entry.targets[i].n_idx == n_idx*/) {
					return entry.targets[i].dst_idx;
				}
			}
			
			// not found, create a new target
			const uint32_t dst = index_counter++;
			model.vertices.push_back(vertices[v_idx - 1].xyz);
			model.tex_coords.push_back(tex_coords[tc_idx - 1].xy);
			model.normals.push_back(float3 {});
			model.binormals.push_back(float3 {});
			model.tangents.push_back(float3 {});
			entry.add(tc_idx, n_idx, dst);
			return dst;
		};
		
		//
		for(const auto& obj : sub_objects) {
			auto sobj = make_unique<obj_model::sub_object>(obj_model::sub_object { .name = obj->name });
			
			sobj->indices.reserve(obj->indices.size());
			for(const auto& triplet : obj->indices) {
				// .obj has the issue that it handles vertex, tex-coord and normal indices separately instead of using a combined index
				// -> map from the triplet to a new index
				
				// 3-triplet / triangle
				if(triplet.vertex.w == 0) {
					sobj->indices.emplace_back(uint3 {
						reassign(triplet.vertex.x, triplet.tex_coord.x, triplet.normal.x),
						reassign(triplet.vertex.y, triplet.tex_coord.y, triplet.normal.y),
						reassign(triplet.vertex.z, triplet.tex_coord.z, triplet.normal.z)
					});
				}
				// 4-triplet / quad
				else {
					const uint4 reassigned_indices {
						reassign(triplet.vertex.x, triplet.tex_coord.x, triplet.normal.x),
						reassign(triplet.vertex.y, triplet.tex_coord.y, triplet.normal.y),
						reassign(triplet.vertex.z, triplet.tex_coord.z, triplet.normal.z),
						reassign(triplet.vertex.w, triplet.tex_coord.w, triplet.normal.w)
					};
					sobj->indices.emplace_back(uint3 { reassigned_indices.x, reassigned_indices.y, reassigned_indices.z });
					sobj->indices.emplace_back(uint3 { reassigned_indices.x, reassigned_indices.z, reassigned_indices.w });
				}
			}
			
			model.objects.emplace_back(move(sobj));
		}
		log_debug("objects done");
		log_debug("indices: %u -> %u", vertices.size(), index_counter);
		
		//
		for(const auto& obj : model.objects) {
			for(const auto& face : obj->indices) {
				const auto delta_1 = model.tex_coords[face.y] - model.tex_coords[face.x];
				const auto delta_2 = model.tex_coords[face.z] - model.tex_coords[face.x];
				const float3 edge1 = model.vertices[face.y] - model.vertices[face.x];
				const float3 edge2 = model.vertices[face.z] - model.vertices[face.x];
				
				float3 norm = edge1.crossed(edge2);
				float3 binormal = (edge1 * delta_2.x) - (edge2 * delta_1.x);
				float3 tangent = (edge1 * delta_2.y) - (edge2 * delta_1.y);
				
				// adjust
				if(norm.dot(tangent.crossed(binormal)) > 0.0f) {
					tangent *= -1.0f;
				}
				else binormal *= -1.0f;
				
				model.normals[face.x] += norm;
				model.normals[face.y] += norm;
				model.normals[face.z] += norm;
				model.binormals[face.x] += binormal;
				model.binormals[face.y] += binormal;
				model.binormals[face.z] += binormal;
				model.tangents[face.x] += tangent;
				model.tangents[face.y] += tangent;
				model.tangents[face.z] += tangent;
			}
		}
		
		for(auto& norm : model.normals) { norm.normalize(); }
		for(auto& binormal : model.binormals) { binormal.normalize(); }
		for(auto& tangent : model.tangents) { tangent.normalize(); }
		
		// process material
		if(mat_filename != "") {
			string prefix = "";
			const auto slash_pos = ctx.tu.file_name.rfind('/');
			if(slash_pos != string::npos) {
				prefix = ctx.tu.file_name.substr(0, slash_pos + 1);
			}
			log_debug("loading mat: %s", mat_filename);
			string mat_data = "";
			if(!file_io::file_to_string(prefix + mat_filename, mat_data)) {
				log_error("failed to load .mtl file: %s", prefix + mat_filename);
				return {};
			}
			
			auto mat_tu = make_unique<translation_unit>(mat_filename);
			mat_tu->source.insert(0, mat_data.c_str(), mat_data.size());
			
			obj_lexer::map_characters(*mat_tu);
			obj_lexer::lex(*mat_tu);
			obj_lexer::assign_token_sub_types(*mat_tu);
			log_debug("mat lexed");
			
			parser_context mtl_parser_ctx { *mat_tu };
			mtl_grammar mtl_grammar_parser;
			auto mats = mtl_grammar_parser.parse(mtl_parser_ctx, success);
			if(!success) {
				log_error("mtl parsing failed");
				return {};
			}
			success = false; // reset for further processing
			log_debug("mtl parsed");
			
			// create a map of all texture file names
			// value: at first use count, later: gl tex id
			unordered_map<string, uint32_t> texture_filenames;
			for(const auto& mat : mats) {
				texture_filenames.emplace(mat.second->ambient, 0);
				texture_filenames.emplace(mat.second->diffuse, 0);
				texture_filenames.emplace(mat.second->specular, 0);
				texture_filenames.emplace(mat.second->normal, 0);
				texture_filenames.emplace(mat.second->mask, 0);
			}
			
			// check use count
			for(const auto& obj : sub_objects) {
				const auto mat_iter = mats.find(obj->mat);
				if(mat_iter == mats.end()) {
					log_error("object \"%s\" references an unknown material \"%s\"!",
							  obj->name, obj->mat);
					continue;
				}
				++texture_filenames[mat_iter->second->ambient];
				++texture_filenames[mat_iter->second->diffuse];
				++texture_filenames[mat_iter->second->specular];
				++texture_filenames[mat_iter->second->normal];
				++texture_filenames[mat_iter->second->mask];
			}
			
			// kill empty string
			const auto empty_iter = texture_filenames.find("");
			if(empty_iter != texture_filenames.end()) texture_filenames.erase(empty_iter);
			
			// kill unused
			core::erase_if(texture_filenames, [](const auto& iter) -> bool {
				if(iter->second == 0) {
					log_debug("killing unused texture \"%s\"", iter->first);
				}
				return (iter->second == 0);
			});
			
			// add dummy textures for later
			texture_filenames.emplace("black.png", 0);
			texture_filenames.emplace("white.png", 0);
			texture_filenames.emplace("up_normal.png", 0);
			
			model.materials.resize(mats.size());
			model.material_infos.resize(mats.size());
			load_textures(texture_filenames, model.textures, prefix);
			
			// assign textures/materials
			unordered_map<string, uint32_t> mat_map; // mat name -> mat idx in model
			uint32_t mat_counter { 0u };
			for(const auto& mat : mats) {
				mat_map[mat.first] = mat_counter++;
			}
			for(const auto& mat : mats) {
				auto& mdl_mat = model.materials[mat_map[mat.first]];
				auto& mdl_mat_info = model.material_infos[mat_map[mat.first]];
				
				mdl_mat_info.name = mat.first;
				mdl_mat_info.diffuse_file_name = (mat.second->diffuse != "" ? mat.second->diffuse : "black.png");
				mdl_mat_info.specular_file_name = (mat.second->specular != "" ? mat.second->specular : "black.png");
				mdl_mat_info.normal_file_name = (mat.second->normal != "" ? mat.second->normal : "up_normal.png");
				mdl_mat_info.mask_file_name = (mat.second->mask != "" ? mat.second->mask : "white.png");
				
				mdl_mat.diffuse = texture_filenames[mdl_mat_info.diffuse_file_name];
				mdl_mat.specular = texture_filenames[mdl_mat_info.specular_file_name];
				mdl_mat.normal = texture_filenames[mdl_mat_info.normal_file_name];
				mdl_mat.mask = texture_filenames[mdl_mat_info.mask_file_name];
			}
			
			// assign materials to sub-objects
			for(size_t i = 0, count = sub_objects.size(); i < count; ++i) {
				model.objects[i]->mat_idx = mat_map[sub_objects[i]->mat];
			}
			log_debug("materials assigned");
		}
		
		// done
		success = true;
		return model;
	}
	
};

obj_model obj_loader::load(const string& file_name, bool& success) {
	success = false;
	
	obj_model model;
	static constexpr const uint32_t bin_obj_version { 1 };
	
	// -> load .obj
	if(!file_io::is_file(file_name + ".bin")) {
		string obj_data = "";
		if(!file_io::file_to_string(file_name, obj_data)) {
			log_error("failed to load .obj file: %s", file_name);
			return {};
		}
		log_debug("loaded");
		
		auto tu = make_unique<translation_unit>(file_name);
		tu->source.insert(0, obj_data.c_str(), obj_data.size());
		
		obj_lexer::map_characters(*tu);
		obj_lexer::lex(*tu);
		obj_lexer::assign_token_sub_types(*tu);
		log_debug("obj lexed");
		
		parser_context parser_ctx { *tu };
		obj_grammar obj_grammar_parser;
		model = obj_grammar_parser.parse(parser_ctx, success);
		if(!success) {
			log_error("obj parsing failed");
			return {};
		}
		log_debug("obj parsed");
		
		// scale vertices
		static constexpr const float scale_factor { 0.1f };
		for(auto& vertex : model.vertices) {
			vertex *= scale_factor;
		}
		
		// write .bin
		file_io bin_file(file_name + ".bin", file_io::OPEN_TYPE::WRITE_BINARY);
		if(!bin_file.is_open()) {
			log_warn("failed to open .bin file!");
		}
		else {
			bin_file.write_uint(bin_obj_version);
			bin_file.write_uint((uint32_t)model.vertices.size());
			bin_file.write_uint((uint32_t)model.objects.size());
			bin_file.write_uint((uint32_t)model.materials.size());
			
			for(const auto& mtl : model.material_infos) {
				bin_file.write_terminated_block(mtl.name, 0);
				bin_file.write_terminated_block(mtl.diffuse_file_name, 0);
				bin_file.write_terminated_block(mtl.specular_file_name, 0);
				bin_file.write_terminated_block(mtl.normal_file_name, 0);
				bin_file.write_terminated_block(mtl.mask_file_name, 0);
			}
			
			for(const auto& obj : model.objects) {
				bin_file.write_terminated_block(obj->name, 0);
				bin_file.write_uint((uint32_t)obj->indices.size());
				bin_file.write_uint(obj->mat_idx);
				for(const auto& idx : obj->indices) {
					bin_file.write_uint(idx.x);
					bin_file.write_uint(idx.y);
					bin_file.write_uint(idx.z);
				}
			}
			
			for(const auto& vtx : model.vertices) {
				bin_file.write_float(vtx.x);
				bin_file.write_float(vtx.y);
				bin_file.write_float(vtx.z);
			}
			for(const auto& tc : model.tex_coords) {
				bin_file.write_float(tc.x);
				bin_file.write_float(tc.y);
			}
			for(const auto& nrm : model.normals) {
				bin_file.write_float(nrm.x);
				bin_file.write_float(nrm.y);
				bin_file.write_float(nrm.z);
			}
			for(const auto& bnm : model.binormals) {
				bin_file.write_float(bnm.x);
				bin_file.write_float(bnm.y);
				bin_file.write_float(bnm.z);
			}
			for(const auto& tgt : model.tangents) {
				bin_file.write_float(tgt.x);
				bin_file.write_float(tgt.y);
				bin_file.write_float(tgt.z);
			}
		}
	}
	// -> load .obj.bin
	else {
		log_debug("loading bin ...");
		
		file_io bin_file(file_name + ".bin", file_io::OPEN_TYPE::READ_BINARY);
		if(!bin_file.is_open()) {
			log_error("failed to open .bin file!");
			return {};
		}
		
		string prefix = "";
		const auto slash_pos = file_name.rfind('/');
		if(slash_pos != string::npos) {
			prefix = file_name.substr(0, slash_pos + 1);
		}
		
		//
		if(bin_file.get_uint() != bin_obj_version) {
			log_error("version mismatch - expected %u!", bin_obj_version);
		}
		
		const auto vertex_count = bin_file.get_uint();
		const auto sub_object_count = bin_file.get_uint();
		const auto mat_count = bin_file.get_uint();
		
		model.vertices.clear();
		model.tex_coords.clear();
		model.normals.clear();
		model.binormals.clear();
		model.tangents.clear();
		model.vertices.reserve(vertex_count);
		model.tex_coords.reserve(vertex_count);
		model.normals.reserve(vertex_count);
		model.binormals.reserve(vertex_count);
		model.tangents.reserve(vertex_count);
		model.materials.reserve(mat_count);
		model.material_infos.reserve(mat_count);
		model.objects.reserve(sub_object_count);
		
		for(uint32_t i = 0; i < mat_count; ++i) {
			model.material_infos.emplace_back(obj_model::material_info {
				bin_file.get_terminated_block(0),
				bin_file.get_terminated_block(0),
				bin_file.get_terminated_block(0),
				bin_file.get_terminated_block(0),
				bin_file.get_terminated_block(0)
			});
		}
		
		unordered_map<string, uint32_t> texture_filenames;
		for(const auto& mat : model.material_infos) {
			texture_filenames.emplace(mat.diffuse_file_name, 0);
			texture_filenames.emplace(mat.specular_file_name, 0);
			texture_filenames.emplace(mat.normal_file_name, 0);
			texture_filenames.emplace(mat.mask_file_name, 0);
		}
		load_textures(texture_filenames, model.textures, prefix);
		
		for(uint32_t i = 0; i < mat_count; ++i) {
			auto& mdl_mat_info = model.material_infos[i];
			model.materials.emplace_back(obj_model::material {
				texture_filenames[mdl_mat_info.diffuse_file_name],
				texture_filenames[mdl_mat_info.specular_file_name],
				texture_filenames[mdl_mat_info.normal_file_name],
				texture_filenames[mdl_mat_info.mask_file_name]
			});
		}
		
		for(uint32_t i = 0; i < sub_object_count; ++i) {
			auto obj = make_unique<obj_model::sub_object>(obj_model::sub_object {
				.name = bin_file.get_terminated_block(0)
			});
			const auto index_count = bin_file.get_uint();
			obj->indices.reserve(index_count);
			
			obj->mat_idx = bin_file.get_uint();
			
			for(uint32_t j = 0; j < index_count; ++j) {
				obj->indices.emplace_back(uint3 {
					bin_file.get_uint(),
					bin_file.get_uint(),
					bin_file.get_uint()
				});
			}
			
			model.objects.emplace_back(move(obj));
		}
		log_debug("loaded sub-objects");
		
		for(uint32_t i = 0; i < vertex_count; ++i) {
			model.vertices.emplace_back(float3 {
				bin_file.get_float(),
				bin_file.get_float(),
				bin_file.get_float()
			});
		}
		for(uint32_t i = 0; i < vertex_count; ++i) {
			model.tex_coords.emplace_back(float2 {
				bin_file.get_float(),
				bin_file.get_float()
			});
		}
		for(uint32_t i = 0; i < vertex_count; ++i) {
			model.normals.emplace_back(float3 {
				bin_file.get_float(),
				bin_file.get_float(),
				bin_file.get_float()
			});
		}
		for(uint32_t i = 0; i < vertex_count; ++i) {
			model.binormals.emplace_back(float3 {
				bin_file.get_float(),
				bin_file.get_float(),
				bin_file.get_float()
			});
		}
		for(uint32_t i = 0; i < vertex_count; ++i) {
			model.tangents.emplace_back(float3 {
				bin_file.get_float(),
				bin_file.get_float(),
				bin_file.get_float()
			});
		}
		
		success = true;
		log_debug("loaded model data");
	}
	
	//
	glGenBuffers(1, &model.vertices_vbo);
	glBindBuffer(GL_ARRAY_BUFFER, model.vertices_vbo);
	glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(model.vertices.size() * sizeof(float3)), &model.vertices[0], GL_STATIC_DRAW);
	glGenBuffers(1, &model.tex_coords_vbo);
	glBindBuffer(GL_ARRAY_BUFFER, model.tex_coords_vbo);
	glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(model.tex_coords.size() * sizeof(float2)), &model.tex_coords[0], GL_STATIC_DRAW);
	glGenBuffers(1, &model.normals_vbo);
	glBindBuffer(GL_ARRAY_BUFFER, model.normals_vbo);
	glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(model.normals.size() * sizeof(float3)), &model.normals[0], GL_STATIC_DRAW);
	glGenBuffers(1, &model.binormals_vbo);
	glBindBuffer(GL_ARRAY_BUFFER, model.binormals_vbo);
	glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(model.binormals.size() * sizeof(float3)), &model.binormals[0], GL_STATIC_DRAW);
	glGenBuffers(1, &model.tangents_vbo);
	glBindBuffer(GL_ARRAY_BUFFER, model.tangents_vbo);
	glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(model.tangents.size() * sizeof(float3)), &model.tangents[0], GL_STATIC_DRAW);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	
	for(auto& obj : model.objects) {
		glGenBuffers(1, &obj->indices_vbo);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, obj->indices_vbo);
		glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)(obj->indices.size() * sizeof(uint3)), &obj->indices[0], GL_STATIC_DRAW);
		obj->triangle_count = (GLsizei)(obj->indices.size() * 3);
	}
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
	
	// clean up mem that isn't needed any more
	glFinish();
	model.vertices.clear();
	model.tex_coords.clear();
	model.normals.clear();
	model.binormals.clear();
	model.tangents.clear();
	model.material_infos.clear();
	for(auto& obj : model.objects) {
		obj->indices.clear();
	}
	
	return model;
}