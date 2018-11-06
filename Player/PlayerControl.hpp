#ifndef PLAYERCONTROL_HPP
#define PLAYERCONTROL_HPP

#include "hwlib.hpp"
#include "rtos.hpp"

#include "MsgListener.hpp"
#include "ButtonListener.hpp"
#include "PlayerData.hpp"
#include "Weapon.hpp"
#include "BuzzerControl.hpp"
#include "GameLogs.hpp"
#include "Weapon.hpp"
#include "PlayerData.hpp"
#include "ShootControl.hpp"
#include "DisplayControl.hpp"

class PlayerControl : public rtos::task<>, public MsgListener, public ButtonListener {
private:
	rtos::channel< ir_msg, 1024 > msg_channel;
	rtos::channel< uint8_t, 1024 > player_number_channel;
	rtos::channel< uint8_t, 1024 > player_weapon_channel;
	rtos::flag trigger_flag;
	rtos::flag reload_flag;
	rtos::flag print_data_flag;
	rtos::timer reload_timer;
	rtos::timer shot_delay_timer;
	rtos::timer death_timer;
	rtos::timer game_timer;
	ShootControl & shoot_control;
	DisplayControl & display_control;
	BuzzerControl & buzzer_control;
	PlayerData & player_data;
	Weapon & weapon;
	GameLogs & game_logs;
	hwlib::pin_out & reload_led;
	hwlib::pin_out & dead_led;
public:
	PlayerControl( const char * name, int priority, ShootControl & shoot_control, DisplayControl & display_control, BuzzerControl & buzzer_control, PlayerData & player_data, Weapon & weapon, GameLogs & game_logs, auto & reload_led, auto & dead_led ):
		task( priority, name ),
		msg_channel( this, "msg_channel" ),
		player_number_channel( this, "player_number_channel"),
		player_weapon_channel( this, "player_weapon_channel"),
		trigger_flag( this, "trigger_flag" ),
		reload_flag( this, "reload_flag" ),
		print_data_flag( this, "print_data_flag" ),
		reload_timer( this, "reload_timer" ),
		shot_delay_timer( this, "shot_delay_timer" ),
		death_timer( this, "death_timer" ),
		game_timer( this, "game_timer" ),
		shoot_control( shoot_control ),
		display_control( display_control ),
		buzzer_control( buzzer_control ),
		player_data( player_data ),
		weapon( weapon ),
		game_logs( game_logs ),
		reload_led( reload_led ),
		dead_led( dead_led )
	{}
	
	void setPlayerNumber( uint8_t player_number ) {
		player_number_channel.write( player_number );
	}
	
	void setWeapon( uint8_t weapon_number ) {
		player_weapon_channel.write( weapon_number );
	}
	
	virtual void msgReceived( const ir_msg & msg ) override {
		msg_channel.write( msg );
	};
	
	virtual void buttonPressed( unsigned int & buttonnumber ) override {
		if( buttonnumber == 0 ){
			trigger_flag.set();
		}
		else if( buttonnumber == 1 ){
			reload_flag.set();
		}
	}
	
	void printByte(uint8_t byte) {
	    for(unsigned int i = 0; i < 8; ++i) {
		    hwlib::cout << ((byte >> (7 - i)) & 1) << ' ';
	    }
	    hwlib::cout << "\n";
    }
	
	void main() override {
		enum states { INIT_GAME, PLAYING };
		enum playing_states { ALIVE_ABLE_TO_SHOOT, ALIVE_NOT_ABLE_TO_SHOOT, DEAD };
		states state = INIT_GAME;
		playing_states playing_state;
		ir_msg msg;
		uint8_t game_length;
		
		for(;;) {
			switch(state) {
				case states::INIT_GAME: {
					auto event = wait( player_number_channel + player_weapon_channel + msg_channel );
					if( event == player_number_channel ) {
						player_data.setPlayerID( player_number_channel.read() );
						display_control.showPlayer( player_data.getPlayerID() );
					}
					else if( event == player_weapon_channel ) {
						weapon.setWeapon( player_weapon_channel.read() );
						display_control.showWeapon( weapon.getWeaponName(weapon.getWeaponID()) );
					}
					else if( event == msg_channel ){
						msg = msg_channel.read();
						if( msg.player == 0 && msg.data < 16 && msg.data > 0 ) {
							game_length = msg.data;
							buzzer_control.makeSound(100);
						}
						else if( msg.player == 0 && msg.data == 0) {
							player_data.setHealth( player_data.getMaxHealth() );
							weapon.setAmmo( weapon.getMaxAmmo() );
							player_data.setDeaths( 0 );
							buzzer_control.makeSound(1000);
							hwlib::wait_ms(1000);
							display_control.showHealth( player_data.getHealth() );
							display_control.showAmmo( weapon.getAmmo() );
							display_control.showDeaths( player_data.getDeaths() );
							hwlib::wait_ms( 10000 );
							for(int i = 5; i >= 0; i--){
								display_control.showCountdown(i);
								hwlib::wait_ms(1100);
							}
							trigger_flag.clear();
							reload_flag.clear();
							msg_channel.clear();
							game_timer.set( (game_length * 60000) * rtos::ms );
							state = states::PLAYING;
							playing_state = ALIVE_ABLE_TO_SHOOT;
						}
					}
					break;
				}
				
				case states::PLAYING: {
					switch(playing_state){
						case playing_states::ALIVE_ABLE_TO_SHOOT: {
							auto event = wait( trigger_flag + reload_flag + msg_channel + game_timer );
							if( event == game_timer ){
								state = states::INIT_GAME;
								game_logs.printLogs();
								game_logs.clearLogs();
							}
							else if( event == trigger_flag && weapon.getAmmo() > 0 ) {
								shoot_control.shoot();
								weapon.setAmmo( weapon.getAmmo() - 1 );
								display_control.showAmmo( weapon.getAmmo() );
								shot_delay_timer.set( (weapon.getShotDelay() * 100) * rtos::ms );
								playing_state = playing_states::ALIVE_NOT_ABLE_TO_SHOOT;
							}
							else if( event == reload_flag ){
								reload_timer.set( (weapon.getReloadTime() * 1000) * rtos::ms );
								weapon.setAmmo( weapon.getMaxAmmo() );
								reload_led.set(1);
								playing_state = playing_states::ALIVE_NOT_ABLE_TO_SHOOT;
							}
							else if( event == msg_channel) {
								msg = msg_channel.read();
								if( msg.player > 0 && msg.player != player_data.getPlayerID() ){
									buzzer_control.makeSound(100);
									game_logs.addLog( msg.player, weapon.getWeaponName(msg.data) );
									if( weapon.getWeaponDamage(msg.data) >= player_data.getHealth() ){
										player_data.setHealth( 0 );
										display_control.showHealth( player_data.getHealth() );
										dead_led.set(1);
										playing_state = playing_states::DEAD;
									}
									else{ 
										player_data.setHealth( (player_data.getHealth() - weapon.getWeaponDamage(msg.data) ) );
										display_control.showHealth( player_data.getHealth() );
									}
								}
							}
							break;
						}
						
						case playing_states::ALIVE_NOT_ABLE_TO_SHOOT: {
							auto event = wait( shot_delay_timer + reload_timer + msg_channel + game_timer );
							if( event == game_timer ){
								state = states::INIT_GAME;
								game_logs.printLogs();
								game_logs.clearLogs();
							}
							else if( event == msg_channel ) {
								msg = msg_channel.read();
								if( msg.player > 0 && msg.player != player_data.getPlayerID() ){
									game_logs.addLog( msg.player, weapon.getWeaponName(msg.data) );
									if( weapon.getWeaponDamage(msg.data) >= player_data.getHealth() ){
										player_data.setHealth( 0 );
										display_control.showHealth( player_data.getHealth() );
										dead_led.set(1);
										playing_state = playing_states::DEAD;
									}
									else{ 
										player_data.setHealth( (player_data.getHealth() - weapon.getWeaponDamage(msg.data) ) );
										display_control.showHealth( player_data.getHealth() );
									}
								}
							}
							else if( event == shot_delay_timer ) {
								trigger_flag.clear();
								playing_state = playing_states::ALIVE_ABLE_TO_SHOOT;
							}
							else if( event == reload_timer ) {
								reload_flag.clear();
								display_control.showAmmo( weapon.getAmmo() );
								reload_led.set(0);
								playing_state = playing_states::ALIVE_ABLE_TO_SHOOT;
							}
							break;
						}
						
						case playing_states::DEAD: {
							death_timer.set( (player_data.getDeathLength() * 1000) * rtos::ms );
							player_data.setDeaths( (player_data.getDeaths() + 1 ) );
							display_control.showDeaths( player_data.getDeaths() );
							auto event = wait( death_timer + game_timer );
							if( event == game_timer ) {
								state = states::INIT_GAME;
								game_logs.printLogs();
								game_logs.clearLogs();
							}
							else if ( event == death_timer ) {
								player_data.setHealth( player_data.getMaxHealth() );
								weapon.setAmmo( weapon.getMaxAmmo() );
								display_control.showHealth( player_data.getHealth() );
								display_control.showAmmo( weapon.getAmmo() );
								msg_channel.clear();
								dead_led.set(0);
								playing_state = playing_states::ALIVE_ABLE_TO_SHOOT;
							}
						}
						break;
					}
					break;
				}
			}
		}
	}
};

#endif