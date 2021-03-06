/*
 * driver/interrupt/interrupt.c
 *
 * Copyright(c) 2007-2017 Jianjun Jiang <8192542@qq.com>
 * Official site: http://xboot.org
 * Mobile phone: +86-18665388956
 * QQ: 8192542
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include <interrupt/interrupt.h>

static void null_interrupt_function(void * data)
{
}

static ssize_t irqchip_read_base(struct kobj_t * kobj, void * buf, size_t size)
{
	struct irqchip_t * chip = (struct irqchip_t *)kobj->priv;
	return sprintf(buf, "%d", chip->base);
}

static ssize_t irqchip_read_nirq(struct kobj_t * kobj, void * buf, size_t size)
{
	struct irqchip_t * chip = (struct irqchip_t *)kobj->priv;
	return sprintf(buf, "%d", chip->nirq);
}

static struct irqchip_t * search_irqchip(int irq)
{
	struct device_t * pos, * n;
	struct irqchip_t * chip;

	list_for_each_entry_safe(pos, n, &__device_head[DEVICE_TYPE_IRQCHIP], head)
	{
		chip = (struct irqchip_t *)(pos->priv);
		if((irq >= chip->base) && (irq < (chip->base + chip->nirq)))
			return chip;
	}
	return NULL;
}

bool_t register_irqchip(struct device_t ** device, struct irqchip_t * chip)
{
	struct device_t * dev;
	int i;

	if(!chip || !chip->name)
		return FALSE;

	if(chip->base < 0 || chip->nirq <= 0)
		return FALSE;

	dev = malloc(sizeof(struct device_t));
	if(!dev)
		return FALSE;

	for(i = 0; i < chip->nirq; i++)
	{
		chip->handler[i].func = null_interrupt_function;
		chip->handler[i].data = NULL;
		if(chip->settype)
			chip->settype(chip, i, IRQ_TYPE_NONE);
		if(chip->disable)
			chip->disable(chip, i);
	}
	dev->name = strdup(chip->name);
	dev->type = DEVICE_TYPE_IRQCHIP;
	dev->priv = chip;
	dev->kobj = kobj_alloc_directory(dev->name);
	kobj_add_regular(dev->kobj, "base", irqchip_read_base, NULL, chip);
	kobj_add_regular(dev->kobj, "nirq", irqchip_read_nirq, NULL, chip);

	if(!register_device(dev))
	{
		kobj_remove_self(dev->kobj);
		free(dev->name);
		free(dev);
		return FALSE;
	}

	if(device)
		*device = dev;
	return TRUE;
}

bool_t unregister_irqchip(struct irqchip_t * chip)
{
	struct device_t * dev;
	int i;

	if(!chip || !chip->name)
		return FALSE;

	if(chip->base < 0 || chip->nirq <= 0)
		return FALSE;

	dev = search_device(chip->name, DEVICE_TYPE_IRQCHIP);
	if(!dev)
		return FALSE;

	if(!unregister_device(dev))
		return FALSE;

	for(i = 0; i < chip->nirq; i++)
	{
		chip->handler[i].func = null_interrupt_function;
		chip->handler[i].data = NULL;
		if(chip->settype)
			chip->settype(chip, i, IRQ_TYPE_NONE);
		if(chip->disable)
			chip->disable(chip, i);
	}

	kobj_remove_self(dev->kobj);
	free(dev->name);
	free(dev);
	return TRUE;
}

bool_t register_sub_irqchip(struct device_t ** device, int parent, struct irqchip_t * chip)
{
	int i;

	if(!chip || !chip->name)
		return FALSE;

	if(chip->base < 0 || chip->nirq <= 0)
		return FALSE;

	for(i = 0; i < chip->nirq; i++)
	{
		chip->handler[i].func = null_interrupt_function;
		chip->handler[i].data = NULL;
		if(chip->settype)
			chip->settype(chip, i, IRQ_TYPE_NONE);
		if(chip->disable)
			chip->disable(chip, i);
	}
	if(!request_irq(parent, (void (*)(void *))(chip->dispatch), IRQ_TYPE_NONE, chip))
		return FALSE;

	chip->dispatch = NULL;
	return register_irqchip(device, chip);
}

bool_t unregister_sub_irqchip(int parent, struct irqchip_t * chip)
{
	if(!chip || !chip->name)
		return FALSE;

	if(chip->base < 0 || chip->nirq <= 0)
		return FALSE;

	if(!free_irq(parent))
		return FALSE;

	return unregister_irqchip(chip);
}

bool_t irq_is_valid(int irq)
{
	return search_irqchip(irq) ? TRUE : FALSE;
}

bool_t request_irq(int irq, void (*func)(void *), enum irq_type_t type, void * data)
{
	struct irqchip_t * chip;
	int offset;

	if(!func)
		return FALSE;

	chip = search_irqchip(irq);
	if(!chip)
		return FALSE;

	offset = irq - chip->base;
	if(chip->handler[offset].func != null_interrupt_function)
		return FALSE;

	chip->handler[offset].func = func;
	chip->handler[offset].data = data;
	if(chip->settype)
		chip->settype(chip, offset, type);
	if(chip->enable)
		chip->enable(chip, offset);

	return TRUE;
}

bool_t free_irq(int irq)
{
	struct irqchip_t * chip;
	int offset;

	chip = search_irqchip(irq);
	if(!chip)
		return FALSE;

	offset = irq - chip->base;
	if(chip->handler[offset].func == null_interrupt_function)
		return FALSE;

	chip->handler[offset].func = null_interrupt_function;
	chip->handler[offset].data = NULL;
	if(chip->settype)
		chip->settype(chip, offset, IRQ_TYPE_NONE);
	if(chip->enable)
		chip->enable(chip, offset);

	return TRUE;
}

void enable_irq(int irq)
{
	struct irqchip_t * chip = search_irqchip(irq);

	if(chip && chip->enable)
		chip->enable(chip, irq - chip->base);
}

void disable_irq(int irq)
{
	struct irqchip_t * chip = search_irqchip(irq);

	if(chip && chip->disable)
		chip->disable(chip, irq - chip->base);
}

void interrupt_handle_exception(void * regs)
{
	struct device_t * pos, * n;
	struct irqchip_t * chip;

	list_for_each_entry_safe(pos, n, &__device_head[DEVICE_TYPE_IRQCHIP], head)
	{
		chip = (struct irqchip_t *)(pos->priv);
		if(chip->dispatch)
			chip->dispatch(chip);
	}
}
