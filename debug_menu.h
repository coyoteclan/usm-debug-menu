#pragma once

#include "mstring.h"

#include <cassert>

constexpr auto EXTEND_NEW_ENTRIES = 20;

constexpr auto MAX_CHARS_SAFE = 63;
constexpr auto MAX_CHARS = MAX_CHARS_SAFE + 1;

enum debug_menu_entry_type {
    NORMAL,
    FLOAT_E,
    POINTER_FLOAT,
    INTEGER,
    POINTER_INT,
    BOOLEAN_E,
    POINTER_BOOL,
    POINTER_MENU,
	CUSTOM
};

enum custom_key_type {
	LEFT,
	RIGHT,
	ENTER
};

struct debug_menu_entry;
typedef char* (*custom_string_generator_ptr)(debug_menu_entry* entry);

struct debug_menu;

struct debug_menu_entry {

	char text[MAX_CHARS];
	debug_menu_entry_type entry_type;
	void* data;
	void* data1;
    uint16_t m_id {0};
	custom_string_generator_ptr custom_string_generator;
    void (*m_game_flags_handler)(debug_menu_entry *) = nullptr;
    float field_20[4] = {0.f, 1.f, 0.1f, 10.f};
    bool m_value_initialized;

    void set_id(int id)
    {
        m_id = id;
    }

    auto get_id() const
    {
        return m_id;
    }

    void on_change(float a3, bool a4)
    {
        switch ( this->entry_type )
        {
        case FLOAT_E:
        case POINTER_FLOAT:
        {
            float v6;
            if ( a4 )
                v6 = this->field_20[2] * this->field_20[3];
            else
                v6 = this->field_20[2];

            auto v5 = this->get_fval() + a3 * v6;
            this->set_fval(v5, true);
            break;
        }
        case BOOLEAN_E:
        case POINTER_BOOL:
        {
            auto v3 = this->get_bval();
            this->set_bval(!v3, true);
            break;
        }
        case INTEGER:
        case POINTER_INT:
        {
            float v7 = (a4 ? this->field_20[2] * this->field_20[3] : this->field_20[2]);

            printf("%f\n", v7);
            auto v8 = std::abs(v7);
            if ( v8 < 1.0 )
            {
                v8 = 1.0;
            }

            auto v4 = this->get_ival();
            if ( a3 >= 0.0 )
                this->set_ival((int)(v4 + v8), true);
            else
                this->set_ival((int)(v4 - v8), true);

            break;
        }
        default:
        return;
        }
    }

    void set_fval(float a2, bool a3)
    {
        if ( !this->is_value_initialized() )
        {
            if ( a2 > this->field_20[1] )
            {
                a2 = this->field_20[1];
            }

            if ( this->field_20[0] > a2 )
            {
                a2 = this->field_20[0];
            }

            auto v3 = this->entry_type;
            if ( v3 == FLOAT_E )
            {
                this->data = bit_cast<void *>(a2);
            }
            else if ( v3 == POINTER_FLOAT )
            {
                *(float *)this->data = a2;
            }
            else
            {
                assert(0);
            }

            if ( this->m_game_flags_handler != nullptr && a3 )
            {
                this->m_game_flags_handler(this);
            }
        }

        this->get_fval();
    }

    float get_fval() {
        auto v2 = this->entry_type;
        if (v2 == FLOAT_E) {
            return bit_cast<float>(this->data);
        }

        if (v2 == POINTER_FLOAT) {
            return *(float *) this->data;
        }

        assert(0);
        return 0.0;
    }

    bool get_bval() const
    {
        auto v2 = this->entry_type;
        if ( v2 == BOOLEAN_E )
        {
            return (bool) this->data;
        }

        if ( v2 == POINTER_BOOL )
        {
            return *(bool *) this->data;
        }

        assert(0);
        return 0;
    }

    int get_ival()
    {
        auto v2 = this->entry_type;
        if ( v2 == INTEGER )
        {
            return (int) this->data;
        }

        if ( v2 == POINTER_INT )
        {
            return *(int*) this->data;
        }

        assert(0);
        return 0;
    }

    bool is_value_initialized() const
    {
        return this->m_value_initialized;
    }

    int set_ival(int a2, bool a3)
    {
        if ( !this->is_value_initialized() )
        {
            if ( (float)a2 > this->field_20[1] )
                a2 = (int)this->field_20[1];

            if ( this->field_20[0] > (float)a2 )
                a2 = (int)this->field_20[0];

            auto v4 = this->entry_type;
            if ( v4 == INTEGER )
            {
                this->data = (void *) a2;
            }
            else if ( v4 == POINTER_INT )
            {
                *((int *) this->data) = a2;
            }
            else
            {
                assert(0);
            }

            if ( this->m_game_flags_handler != nullptr && a3 )
            {
                this->m_game_flags_handler(this);
            }
        }

        return this->get_ival();
    }

    void set_p_ival(int *a2)
    {
        this->entry_type = POINTER_INT;
        this->data = (void *) a2;
    }

    void set_pt_fval(float *a2)
    {
        this->entry_type = POINTER_FLOAT;
        this->data = bit_cast<void *>(a2);
    }

    void set_min_value(float a2)
    {
        this->field_20[0] = a2;
    }

    void set_max_value(float a2)
    {
        this->field_20[1] = a2;
    }

    void set_bval(bool a2)
    {
        this->entry_type = BOOLEAN_E;
        this->data = (void *) a2;
    }

    bool set_bval(bool a2, bool a3)
    {
        if ( !this->is_value_initialized() )
        {
            auto v4 = this->entry_type;
            if ( v4 == BOOLEAN_E )
            {
                this->data = (void *) a2;
            }
            else if ( v4 == POINTER_BOOL )
            {
                * ((BOOL *) this->data) = a2;
            }
            else
            {
                assert(0);
            }

            if ( this->m_game_flags_handler != nullptr && a3 )
            {
                this->m_game_flags_handler(this);
            }
        }

        return this->get_bval();
    }

    void set_pt_bval(bool *a2)
    {
        this->entry_type = POINTER_BOOL;
        this->data = a2;
    }

    void set_ival(int a2)
    {
        this->entry_type = INTEGER;
        this->data = (void *) a2;
    }

    void set_fl_values(const float *a2)
    {
        auto *v2 = this->field_20;
        v2[0] = a2[0];
        v2[1] = a2[1];
        v2[2] = a2[2];
        v2[3] = a2[3];
    }

    void set_game_flags_handler(void (*a2)(debug_menu_entry *))
    {
        this->m_game_flags_handler = a2;
    }

    debug_menu_entry() = default;

    debug_menu_entry(const char *p_text) : entry_type(NORMAL), data(nullptr)
    {
        strncpy(this->text, p_text, MAX_CHARS_SAFE);
    }

    debug_menu_entry(const mString &p_text) : entry_type(NORMAL), data(nullptr)
    {
        strncpy(this->text, p_text.c_str(), MAX_CHARS_SAFE);
    }

    debug_menu_entry(debug_menu *submenu);

    debug_menu_entry(const char *p_text, debug_menu_entry_type p_type, void *p_data) :
        entry_type(p_type), data(p_data)
    {
        strncpy(this->text, p_text, MAX_CHARS_SAFE);
    }
};

typedef void (*menu_handler_function)(debug_menu_entry*, custom_key_type key_type);

void close_debug();

debug_menu* current_menu = nullptr;

struct debug_menu {
	char title[MAX_CHARS];
	DWORD capacity;
	DWORD used_slots;
	DWORD window_start;
	DWORD cur_index;
	menu_handler_function handler;
	debug_menu_entry* entries;
    debug_menu *m_parent {nullptr};

    void add_entry(debug_menu_entry *entry);

    static void hide()
    {
        close_debug();
    }

    void go_back()
    {
        if (this->m_parent != nullptr)
        { 
            current_menu = this->m_parent;
            return;
        }
        
        close_debug();
    }

    static inline bool physics_state_on_exit = true;
};

debug_menu_entry::debug_menu_entry(debug_menu *submenu) : entry_type(POINTER_MENU), data(submenu)
{
    strncpy(this->text, submenu->title, MAX_CHARS_SAFE);
}

void* add_debug_menu_entry(debug_menu* menu, debug_menu_entry* entry) {

    if (entry->entry_type == POINTER_MENU)
    {
        auto *submenu = (debug_menu *) entry->data;
        assert(submenu != nullptr);

        submenu->m_parent = menu;
    }

	if (menu->used_slots < menu->capacity) {
		void* ret = &menu->entries[menu->used_slots];
		memcpy(ret, entry, sizeof(debug_menu_entry));
		++menu->used_slots;
		return ret;
	}
	else {
		DWORD current_entries_size = sizeof(debug_menu_entry) * menu->capacity;
		DWORD new_entries_size = sizeof(debug_menu_entry) * EXTEND_NEW_ENTRIES;

		void* new_ptr = realloc(menu->entries, current_entries_size + new_entries_size);

		if (new_ptr == nullptr) {
			printf("RIP\n");
			__debugbreak();
		}
		else {

			menu->capacity += EXTEND_NEW_ENTRIES;
			menu->entries = static_cast<decltype(menu->entries)>(new_ptr);
			memset(&menu->entries[menu->used_slots], 0, new_entries_size);

			return add_debug_menu_entry(menu, entry);
		}
	}
	
	return nullptr;
}

void debug_menu::add_entry(debug_menu_entry *entry)
{
    add_debug_menu_entry(this, entry);
}


debug_menu* create_menu(const char* title, menu_handler_function function, DWORD capacity) {

	auto *mem = malloc(sizeof(debug_menu));
    debug_menu* menu = static_cast<debug_menu*>(mem);
	memset(menu, 0, sizeof(debug_menu));

	strncpy(menu->title, title, MAX_CHARS_SAFE);

	menu->capacity = capacity;
	menu->handler = function;
	DWORD total_entries_size = sizeof(debug_menu_entry) * capacity;
	menu->entries = static_cast<decltype(menu->entries)>(malloc(total_entries_size));
	memset(menu->entries, 0, total_entries_size);

	return menu;
}


void handle_game_entry(debug_menu_entry *entry, custom_key_type key_type)
{
    printf("entry->text = %s\n", entry->text);

    if (key_type == ENTER)
    {
        switch(entry->entry_type)
        {
        case BOOLEAN_E: 
        case POINTER_BOOL:
        {
            auto v3 = entry->get_bval();
            entry->set_bval(!v3, true);
            break;
        } 
        case POINTER_MENU:
        {
            if (entry->data != nullptr)
            {
                current_menu = static_cast<decltype(current_menu)>(entry->data);
            }
            return;
        }
        default:
            break;
        }
    }
    else if (key_type == LEFT || key_type == RIGHT)
    {
        if (key_type == LEFT)
        {
            entry->on_change(-1.0, true);
        }
        else if (key_type == RIGHT)
        {
            entry->on_change(1.0, true);
        }
    }
}
