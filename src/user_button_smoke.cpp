#if defined(USER_BUTTON_SMOKE) && USER_BUTTON_SMOKE

#include "stm32f4xx_hal.h"
#include "user_button_debounce.h"

#include <cstring>

namespace
{
UART_HandleTypeDef buttonUart = {};
constexpr uint32_t PollIntervalMs = 20;

bool UART1_Init()
{
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_USART1_CLK_ENABLE();

    GPIO_InitTypeDef gpio = {};
    gpio.Pin = GPIO_PIN_9 | GPIO_PIN_10;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate = GPIO_AF7_USART1;
    HAL_GPIO_Init(GPIOA, &gpio);

    buttonUart.Instance = USART1;
    buttonUart.Init.BaudRate = 115200;
    buttonUart.Init.WordLength = UART_WORDLENGTH_8B;
    buttonUart.Init.StopBits = UART_STOPBITS_1;
    buttonUart.Init.Parity = UART_PARITY_NONE;
    buttonUart.Init.Mode = UART_MODE_TX_RX;
    buttonUart.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    buttonUart.Init.OverSampling = UART_OVERSAMPLING_16;
    return HAL_UART_Init(&buttonUart) == HAL_OK;
}

void uartPrint(const char *text)
{
    HAL_UART_Transmit(&buttonUart,
                     reinterpret_cast<uint8_t *>(const_cast<char *>(text)),
                     static_cast<uint16_t>(std::strlen(text)), HAL_MAX_DELAY);
}

void UserButton_Init()
{
    __HAL_RCC_GPIOA_CLK_ENABLE();
    GPIO_InitTypeDef gpio = {};
    gpio.Pin = GPIO_PIN_0;
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(GPIOA, &gpio);
}

bool userButtonPressed()
{
    return HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_0) == GPIO_PIN_RESET;
}
} // namespace

// Standalone HAL timing only; no button interrupt, EXTI or RTOS kernel.
extern "C" void SysTick_Handler(void)
{
    HAL_IncTick();
}

int main()
{
    HAL_Init();
    SystemCoreClockUpdate();
    if (!UART1_Init())
    {
        for (;;) HAL_Delay(1000);
    }
    UserButton_Init();
    HAL_Delay(1); // Let the newly enabled internal pull-up settle before seeding.

    const bool heldAtBoot = userButtonPressed();
    UserButtonDebounce button;
    button.initialize(heldAtBoot, HAL_GetTick());

    uartPrint("\r\nUSER_BUTTON SMOKE TEST\r\n"
              "PA0 configured INPUT_PULLUP\r\n"
              "Released level: HIGH\r\n"
              "Pressed level: LOW\r\n");
    uartPrint(heldAtBoot ? "BOOT: BUTTON HELD - RELEASE BEFORE NEXT PRESS\r\n"
                         : "BOOT: BUTTON RELEASED\r\n");

    for (;;)
    {
        HAL_Delay(PollIntervalMs);
        const bool pressed = userButtonPressed();
        const UserButtonTransition transition = button.update(pressed, HAL_GetTick());
        if (transition == UserButtonTransition::Pressed)
            uartPrint("USER_BUTTON: PRESSED\r\n");
        else if (transition == UserButtonTransition::Released)
            uartPrint("USER_BUTTON: RELEASED\r\n");
    }
}

#endif // USER_BUTTON_SMOKE
