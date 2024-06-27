/**
 * NetXMS - open source network management system
 * Copyright (C) 2003-2021 Victor Kirhenshtein
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
package org.netxms.nxmc.modules.objects;

import java.util.Set;
import org.netxms.client.objects.AbstractObject;
import org.netxms.nxmc.base.views.PerspectiveConfiguration;
import org.netxms.nxmc.localization.LocalizationHelper;
import org.netxms.nxmc.modules.objects.views.ObjectBrowser;
import org.netxms.nxmc.resources.ResourceManager;
import org.xnap.commons.i18n.I18n;

/**
 * "Infrastructure" perspective
 */
public class InfrastructurePerspective extends ObjectsPerspective
{
   public static final I18n i18n = LocalizationHelper.getI18n(InfrastructurePerspective.class);

   private static final Set<Integer> classFilterInfrastructure = ObjectBrowser.calculateClassFilter(SubtreeType.INFRASTRUCTURE);
   private static final Set<Integer> classFilterNetwork = ObjectBrowser.calculateClassFilter(SubtreeType.NETWORK);

   /**
    * Create "Infrastructure" perspective
    */
   public InfrastructurePerspective()
   {
      super("objects.infrastructure", i18n.tr("Infrastructure"), ResourceManager.getImage("icons/perspective-infrastructure.png"), SubtreeType.INFRASTRUCTURE,
            (AbstractObject o) -> {
               if (!o.hasParents() || (o.getObjectClass() == AbstractObject.OBJECT_CONTAINER) || (o.getObjectClass() == AbstractObject.OBJECT_COLLECTOR) ||
                     (o.getObjectClass() == AbstractObject.OBJECT_WIRELESSDOMAIN) || (o.getObjectClass() == AbstractObject.OBJECT_CONDITION))
                  return true;
               if (o.getObjectClass() == AbstractObject.OBJECT_SUBNET)
                  return o.hasAccessibleParents(classFilterInfrastructure);
               return o.hasAccessibleParents(classFilterInfrastructure) || !o.hasAccessibleParents(classFilterNetwork);
            });
   }

   /**
    * @see org.netxms.nxmc.modules.objects.ObjectsPerspective#configurePerspective(org.netxms.nxmc.base.views.PerspectiveConfiguration)
    */
   @Override
   protected void configurePerspective(PerspectiveConfiguration configuration)
   {
      super.configurePerspective(configuration);
      configuration.priority = 10;
   }
}
